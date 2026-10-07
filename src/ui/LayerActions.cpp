#include "MainWindow.h"
#include "LayerPanel.h"
#include "LayerCopyCommit.h"
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QInputDialog>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QColorDialog>
#include <QToolButton>
#include <QMenu>
#include <algorithm>

namespace compositor {
layers::SelectionState MainWindow::layerSelection()const{
    int index=tabs_->currentIndex();if(index<0||index>=int(projects_.size())||!projects_[index]->document)return {};
    const auto&p=*projects_[index];return layers::normalizeSelection(*p.document,{p.selected.empty()?std::vector<std::string>{p.active}:p.selected,p.active});
}
void MainWindow::applyLayerEdit(layers::EditResult result){
    if(!result.changed)return;edit(result.action.c_str(),[&](Document&d){d=std::move(result.document);current()->active=result.selection.primary;current()->selected=result.selection.ids;});
}
void MainWindow::newBlankLayer(){
    if(!canEditLayers())return;
    edit("新建空白图层",[this](Document& document){
        Layer layer;layer.id=newId();layer.transform={0,0,double(document.width),double(document.height)};
        int number=1;while(std::any_of(document.layers.begin(),document.layers.end(),[&](const Layer& item){return item.name=="图层 "+std::to_string(number);}))++number;
        layer.name="图层 "+std::to_string(number);
        auto selected=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& item){return item.id==current()->active;});
        size_t insertion=document.layers.size();
        if(selected!=document.layers.end()){
            insertion=size_t(selected-document.layers.begin())+1;layer.parentId=selected->group?selected->id:selected->parentId;
            if(selected->group){const auto children=layers::descendants(document,selected->id);for(size_t i=0;i<document.layers.size();++i)if(children.contains(document.layers[i].id))insertion=std::max(insertion,i+1);}
        }
        if(!layer.parentId.empty())current()->collapsedGroups.erase(layer.parentId);
        current()->active=layer.id;current()->selected={layer.id};current()->maskSelected=false;
        document.layers.insert(document.layers.begin()+insertion,std::move(layer));
    });
}
void MainWindow::finishVisibilitySwipe(){
    auto* owner=visibilityOwner_;visibilityOwner_=nullptr;
    if(owner){
        try{owner->history.end(owner->document,owner->active);}
        catch(...){if(auto snapshot=owner->history.cancel()){owner->document=std::move(snapshot->document);owner->active=std::move(snapshot->activeLayer);}throw;}
        if(owner==current())refresh();
    }
}
void MainWindow::layerCommand(int command){
    applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();pointerCancel();
    auto*p=current();if(!p||!p->document)return;const auto&d=*p->document;auto selection=layerSelection();
    switch(command){
        case 0:applyLayerEdit(layers::addGroup(d,selection));break;
        case 1:applyLayerEdit(layers::group(d,selection));break;
        case 2:applyLayerEdit(layers::moveOut(d,selection));break;
        case 3:applyLayerEdit(layers::duplicateLayer(d,selection,p->active));break;
        case 4:applyLayerEdit(layers::moveSibling(d,selection,1));break;
        case 5:applyLayerEdit(layers::moveSibling(d,selection,-1));break;
        case 6:{auto plan=layers::deletionPlan(d,selection);auto mode=layers::DeleteMode::Bake;
            if(!plan.dependents.empty()){QMessageBox box(QMessageBox::Question,"删除图层","其他图层将所选图层用作剪贴蒙版源。",QMessageBox::Cancel,this);auto*bake=box.addButton("合并外观",QMessageBox::AcceptRole);auto*release=box.addButton("移除剪贴蒙版链接",QMessageBox::DestructiveRole);box.exec();if(box.clickedButton()==bake)mode=layers::DeleteMode::Bake;else if(box.clickedButton()==release)mode=layers::DeleteMode::RemoveLinks;else return;}
            applyLayerEdit(layers::erase(d,selection,mode));break;}
        case 7:applyLayerEdit(layers::toggleClipping(d,selection,p->active));break;
        case 8:mergeLayers();break;
        case 9:{
            QStringList titles{"新建项目"};std::vector<QPointer<QObject>> identities{nullptr};
            for(int i=0;i<int(projects_.size());++i)if(projects_[i].get()!=p&&!projects_[i]->importing&&!projects_[i]->projectBusy){identities.push_back(projects_[i]->canvas);titles.append(tabs_->tabText(i));}
            QInputDialog dialog(this);dialog.setWindowTitle("复制图层到项目");dialog.setLabelText("目标项目");dialog.setComboBoxItems(titles);dialog.setComboBoxEditable(false);
            auto* choices=dialog.findChild<QComboBox*>();if(!choices)throw std::runtime_error("Copy destination control is unavailable");
            for(int i=0;i<int(identities.size());++i)choices->setItemData(i,QVariant::fromValue(identities[size_t(i)].data()));
            if(dialog.exec()!=QDialog::Accepted)return;
            auto* identity=choices->currentData().value<QObject*>();EditorProject* target=nullptr;
            if(identity){if(choices->currentIndex()<1||identities[size_t(choices->currentIndex())].data()!=identity)return;for(auto& project:projects_)if(project->canvas==identity){target=project.get();break;}if(!target||target->importing||target->projectBusy)return;}
            else{if(choices->currentIndex()!=0)return;target=&addEmptyProject(false);}
            const bool first=!target->document;Document destination;
            if(target->document)destination=*target->document;
            else{destination.id=newId();destination.width=d.width;destination.height=d.height;Layer blank;blank.id=newId();blank.name="图层 1";blank.transform={0,0,double(d.width),double(d.height)};destination.layers.push_back(blank);}
            auto result=layers::copySubtree(d,p->active,destination);
            ui::commitPreparedLayerCopy(*target,target->document,target->active,result.edit);
            for(int i=0;i<int(projects_.size());++i)if(projects_[i].get()==target)tabs_->setCurrentIndex(i);refresh();if(first)target->canvas->fit();break;
        }
        case 10:QTimer::singleShot(0,this,[this]{if(auto*item=layers_->currentItem())layers_->editItem(item,0);});break;
        case 11:if(active())edit("图层可见性",[this](Document&){active()->visible=!active()->visible;});break;
        case 12:applyLayerEdit(layers::releaseClipping(d,selection,p->active));break;
        case 13:applyLayerEdit(layers::ungroup(d,selection));break;
    }
}
void MainWindow::setupLayerActions(){
    QMenu*menu=nullptr;for(auto*a:menuBar()->actions())if(a->text()=="图层")menu=a->menu();if(!menu)return;
    action(menu,"新建组",{},[this]{layerCommand(0);});action(menu,"编组所选",QKeySequence("Ctrl+G"),[this]{layerCommand(1);});action(menu,"取消编组",QKeySequence("Shift+Ctrl+G"),[this]{layerCommand(13);});action(menu,"移出组",{},[this]{layerCommand(2);});
    action(menu,"文字图层…",{},[this]{if(!current()||!current()->document)return;selectTool(Tool::Text);beginText({32,32},true);});
    action(menu,"创建/释放剪贴蒙版",QKeySequence("Ctrl+Alt+G"),[this]{layerCommand(7);});action(menu,"合并图层",QKeySequence("Ctrl+E"),[this]{layerCommand(8);});action(menu,"复制图层到项目…",{},[this]{layerCommand(9);});
    ui::LayerPanelController::Host host;
    host.dragOwner=[this]()->QObject*{auto*p=current();return p?p->canvas:nullptr;};
    host.prepare=[this]{if(refreshing_)return;applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();finishOpacityEdit();pointerCancel();};
    host.state=[this]{auto*p=current();return ui::PanelState{p&&p->document?&*p->document:nullptr,layerSelection(),p&&p->maskSelected,p?p->collapsedGroups:std::unordered_set<std::string>{}};};
    host.select=[this](layers::SelectionState selection,bool mask){auto*p=current();if(refreshing_||!p||!p->document||p->importing||p->projectBusy||selection.ids.empty())return;selection=layers::normalizeSelection(*p->document,std::move(selection));p->active=selection.primary;p->selected=selection.ids;p->maskSelected=mask&&selection.ids.size()==1&&active()&&active()->mask.has_value();refresh(false,false);if(auto*c=ui::LayerPanelController::find(layers_))c->updateSelection();};
    host.commit=[this](layers::EditResult result,bool mask){if(auto*p=current()){p->maskSelected=mask;applyLayerEdit(std::move(result));}};
    host.collapse=[this](const std::string& id,bool collapsed){auto*p=current();if(!p||!p->document)return;if(collapsed){auto children=layers::descendants(*p->document,id);if(children.contains(p->active)){p->active=id;p->selected={id};p->maskSelected=false;}p->collapsedGroups.insert(id);}else p->collapsedGroups.erase(id);if(auto*c=ui::LayerPanelController::find(layers_))c->updateSelection();};
    host.layerCommand=[this](int command){layerCommand(command);};host.maskCommand=[this](int command){maskCommand(command);};host.loadSelection=[this](const std::string&id,bool mask,Qt::KeyboardModifiers modifiers){loadLayerSelection(id,mask,modifiers);};host.error=[this](const QString& error){statusBar()->showMessage(error);};
    host.canEdit=[this]{return canEditLayers();};
    host.targetEnabled=[this]{auto* project=current();if(!project||!project->document)return false;const auto state=commandState(project);return !state.modalDialog&&!state.projectBusy&&!state.importing;};
    host.selectTarget=[this](const std::string& id,bool mask){selectLayerTarget(id,mask);};
    host.beginVisibilitySwipe=[this](const std::string& id)->std::optional<bool>{
        if(!canEditLayers())return {};finishVisibilitySwipe();finishOpacityEdit();
        auto* project=current();auto& document=*project->document;
        auto found=std::find_if(document.layers.begin(),document.layers.end(),[&](const Layer& layer){return layer.id==id;});if(found==document.layers.end())return {};
        const bool visible=!found->visible;visibilityOwner_=project;project->history.begin(visible?"显示图层":"隐藏图层",project->document,project->active);
        found->visible=visible;refresh(false,false);return visible;
    };
    host.setVisibilityInSwipe=[this](const std::string& id,bool visible){
        if(!visibilityOwner_||!visibilityOwner_->document)return;
        for(auto& layer:visibilityOwner_->document->layers)if(layer.id==id){layer.visible=visible;break;}
        if(visibilityOwner_==current())refresh(false,false);
    };
    host.endVisibilitySwipe=[this]{finishVisibilitySwipe();};
    new ui::LayerPanelController(layers_,std::move(host));
    disconnect(layers_,&QTreeWidget::itemChanged,this,nullptr);
    connect(layers_,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem* item,int column){
        auto* owner=current();if(refreshing_||column!=0||!item||!owner||!owner->document||!canEditLayers())return;
        const auto id=item->data(0,Qt::UserRole).toString().toStdString();
        const auto name=item->text(0).trimmed().toStdString();const bool visible=item->checkState(0)==Qt::Checked;
        const auto documentId=owner->document->id;QPointer<NativeCanvas> token=owner->canvas;
        // A refresh rebuilds the tree. Qt still accesses the item after emitting
        // itemChanged from setData, so retain values and finish that call first.
        QTimer::singleShot(0,this,[this,owner,token,documentId,id,name,visible]{
            if(!token||current()!=owner||!owner->document||owner->document->id!=documentId||!canEditLayers())return;
            try{
                auto found=std::find_if(owner->document->layers.begin(),owner->document->layers.end(),[&](const Layer& layer){return layer.id==id;});
                if(found==owner->document->layers.end())return;
                const bool renamed=!name.empty()&&name!=found->name;if(!renamed&&visible==found->visible)return;
                applyGradient();if(transformSession_&&transformSession_->persistent)applyTransformSession();pointerCancel();
                edit(renamed?"重命名图层":"图层可见性",[&](Document& document){for(auto& layer:document.layers)if(layer.id==id){layer.visible=visible;if(!name.empty())layer.name=name;}});
            }catch(const std::exception& error){statusBar()->showMessage(QString::fromUtf8(error.what()));}
        });
    });
    connect(layers_,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem*item,int column){if(!item||column!=0)return;const auto id=item->data(0,Qt::UserRole).toString().toStdString();selectLayerTarget(id,false);if(active()&&!active()->adjustmentJson.empty())adjust({},true,true);else if(auto* current=layers_->currentItem())layers_->editItem(current,0);});
    auto*masks=menu->addMenu("蒙版");masks->setObjectName("layerMaskMenu");
    struct Entry{const char* text;const char* name;ui::MaskCommand command;};
    for(const auto& entry:std::vector<Entry>{{"添加白色蒙版（显示选区）","maskAddReveal",ui::MaskCommand::AddReveal},{"添加黑色蒙版（隐藏选区）","maskAddHide",ui::MaskCommand::AddHide},{"全部显示","maskRevealAll",ui::MaskCommand::RevealAll},{"全部隐藏","maskHideAll",ui::MaskCommand::HideAll},{"启用/停用蒙版","maskToggleEnabled",ui::MaskCommand::ToggleEnabled},{"链接/取消链接蒙版","maskToggleLink",ui::MaskCommand::ToggleLink},{"删除蒙版","maskDelete",ui::MaskCommand::Delete},{"编辑图像","maskEditImage",ui::MaskCommand::EditImage},{"编辑蒙版","maskEditMask",ui::MaskCommand::EditMask},{"选择图像不透明区域","maskLoadAlpha",ui::MaskCommand::LoadAlpha},{"选择蒙版黑色区域","maskLoadBlack",ui::MaskCommand::LoadBlack}}){auto*a=action(masks,entry.text,{},[this,command=entry.command]{maskCommand(int(command));});a->setObjectName(entry.name);a->setProperty("maskCommand",int(entry.command));}

    auto*parent=layers_->parentWidget();auto*layout=qobject_cast<QVBoxLayout*>(parent->layout());if(layout){auto*editTarget=new QComboBox(parent);editTarget->setObjectName("layerEditTarget");editTarget->setAccessibleName("图层编辑目标");editTarget->addItems({"编辑图像","编辑蒙版"});layout->addWidget(editTarget);editTarget->hide();connect(editTarget,&QComboBox::currentIndexChanged,this,[this](int value){if(!refreshing_)maskCommand(int(value?ui::MaskCommand::EditMask:ui::MaskCommand::EditImage));});auto*footer=new QHBoxLayout;auto addButton=[&](QString text,QString name,std::function<void()> fn){auto*button=new QPushButton(text,parent);button->setObjectName(name);button->setAccessibleName(text);connect(button,&QPushButton::clicked,this,[this,fn]{try{fn();}catch(const std::exception&e){statusBar()->showMessage(e.what());}});footer->addWidget(button);};addButton("组","layerGroupButton",[this]{layerCommand(1);});addButton("添加蒙版","layerAddMaskButton",[this]{maskCommand(int(ui::MaskCommand::AddReveal));});auto*effectsButton=new QToolButton(parent);effectsButton->setObjectName("layerEffectsButton");effectsButton->setText("效果");effectsButton->setAccessibleName("效果");effectsButton->setPopupMode(QToolButton::InstantPopup);auto*effectsMenu=new QMenu(effectsButton);effectsMenu->setObjectName("layerEffectsMenu");const std::pair<const char*,int> effectKinds[]={{"描边",0},{"投影",1},{"颜色叠加",2},{"内阴影",3},{"外发光",4},{"内发光",5}};for(const auto& kind:effectKinds){auto*item=effectsMenu->addAction(kind.first);connect(item,&QAction::triggered,this,[this,which=kind.second]{editLayerEffect(which);});}effectsButton->setMenu(effectsMenu);footer->addWidget(effectsButton);addButton("删除","layerDeleteButton",[this]{auto*p=current();if(p&&p->maskSelected&&layerSelection().ids.size()==1)maskCommand(int(ui::MaskCommand::Delete));else layerCommand(6);});layout->addLayout(footer);}}
void MainWindow::editLayerEffect(int kind){
    auto* project=current();if(!project||!project->document||!canEditLayers())return;
    const auto id=project->active;if(id.empty())return;
    auto locate=[&]()->Layer*{auto found=std::find_if(project->document->layers.begin(),project->document->layers.end(),[&](const Layer& layer){return layer.id==id;});return found==project->document->layers.end()||found->group?nullptr:&*found;};
    auto* layer=locate();if(!layer)return;
    const Document original=*project->document;
    const char* titles[]={"描边","投影","颜色叠加","内阴影","外发光","内发光"};
    QDialog dialog(this);dialog.setWindowTitle(titles[std::clamp(kind,0,5)]);dialog.setObjectName("layerEffectDialog");
    auto* form=new QFormLayout(&dialog);
    auto* enabled=new QCheckBox("启用",&dialog);enabled->setChecked(true);
    auto* opacity=new QDoubleSpinBox(&dialog);opacity->setRange(0,1);opacity->setSingleStep(.05);opacity->setDecimals(2);
    auto* size=new QDoubleSpinBox(&dialog);size->setRange(0,250);size->setDecimals(1);
    auto* angle=new QDoubleSpinBox(&dialog);angle->setRange(-360,360);angle->setDecimals(1);
    auto* distance=new QDoubleSpinBox(&dialog);distance->setRange(0,30000);distance->setDecimals(1);
    auto* position=new QComboBox(&dialog);position->addItems({"外部","居中","内部"});
    auto* blend=new QComboBox(&dialog);for(const char* name:ui::zhBlendNames)blend->addItem(name);
    auto* colorButton=new QPushButton("颜色",&dialog);
    Pixel color{0,0,0,255};
    if(kind==0){enabled->setChecked(layer->effects.stroke.enabled||!layer->effects.specified);size->setValue(layer->effects.stroke.size);position->setCurrentIndex(layer->effects.stroke.position);color=layer->effects.stroke.color;}
    else if(kind==1||kind==3){auto& shadow=kind==1?layer->effects.dropShadow:layer->effects.innerShadow;enabled->setChecked(shadow.enabled||!layer->effects.specified);opacity->setValue(shadow.opacity);angle->setValue(shadow.angle);distance->setValue(shadow.distance);size->setValue(shadow.size);blend->setCurrentIndex(int(shadow.blend));color=shadow.color;}
    else if(kind==4||kind==5){auto& glow=kind==4?layer->effects.outerGlow:layer->effects.innerGlow;enabled->setChecked(glow.enabled||!layer->effects.specified);opacity->setValue(glow.opacity);size->setValue(glow.size);blend->setCurrentIndex(int(glow.blend));color=glow.color;}
    else{enabled->setChecked(layer->effects.colorOverlay.enabled||!layer->effects.specified);opacity->setValue(layer->effects.colorOverlay.opacity);blend->setCurrentIndex(int(layer->effects.colorOverlay.blend));color=layer->effects.colorOverlay.color;}
    auto paint=[&]{colorButton->setStyleSheet(QString("background:%1").arg(QColor(color.r,color.g,color.b).name()));};
    paint();
    auto republish=[&]{
        layer=locate();if(!layer)return;
        layer->effects.specified=true;
        if(kind==0){layer->effects.stroke.enabled=enabled->isChecked();layer->effects.stroke.size=size->value();layer->effects.stroke.position=position->currentIndex();layer->effects.stroke.color=color;}
        else if(kind==1||kind==3){auto& shadow=kind==1?layer->effects.dropShadow:layer->effects.innerShadow;shadow.enabled=enabled->isChecked();shadow.opacity=opacity->value();shadow.angle=angle->value();shadow.distance=distance->value();shadow.size=size->value();shadow.blend=Blend(blend->currentIndex());shadow.color=color;}
        else if(kind==4||kind==5){auto& glow=kind==4?layer->effects.outerGlow:layer->effects.innerGlow;glow.enabled=enabled->isChecked();glow.opacity=opacity->value();glow.size=size->value();glow.blend=Blend(blend->currentIndex());glow.color=color;}
        else{layer->effects.colorOverlay.enabled=enabled->isChecked();layer->effects.colorOverlay.opacity=opacity->value();layer->effects.colorOverlay.blend=Blend(blend->currentIndex());layer->effects.colorOverlay.color=color;}
        if(textSession_&&textSession_->owner==project&&textSession_->layerId==id)publishTextEdit();
        else refresh(false,false);
    };
    form->addRow(enabled);
    if(kind!=0)form->addRow("不透明度",opacity);
    if(kind!=2)form->addRow("大小",size);
    if(kind==1||kind==3){form->addRow("角度",angle);form->addRow("距离",distance);}
    if(kind==0)form->addRow("位置",position);
    if(kind!=0)form->addRow("混合",blend);
    form->addRow(colorButton);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(enabled,&QCheckBox::toggled,&dialog,republish);connect(opacity,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,republish);connect(size,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,republish);connect(angle,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,republish);connect(distance,qOverload<double>(&QDoubleSpinBox::valueChanged),&dialog,republish);connect(position,&QComboBox::currentIndexChanged,&dialog,republish);connect(blend,&QComboBox::currentIndexChanged,&dialog,republish);
    connect(colorButton,&QPushButton::clicked,&dialog,[&]{auto chosen=QColorDialog::getColor(QColor(color.r,color.g,color.b),&dialog,"效果颜色");if(!chosen.isValid())return;color={uint8_t(chosen.red()),uint8_t(chosen.green()),uint8_t(chosen.blue()),255};paint();republish();});
    republish();
    if(dialog.exec()!=QDialog::Accepted){*project->document=original;if(textSession_&&textSession_->owner==project&&textSession_->layerId==id)publishTextEdit();else refresh(false,false);return;}
    project->history.begin(titles[std::clamp(kind,0,5)],original,project->active);project->history.end(project->document,project->active);refresh();
}
void MainWindow::refreshLayerPanel(){if(auto* controller=ui::LayerPanelController::find(layers_))controller->rebuild();}
}
