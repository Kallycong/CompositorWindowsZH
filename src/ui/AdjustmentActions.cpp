#include "MainWindow.h"
#include "AdjustmentDialog.h"
#include "SubjectPanel.h"
#include "effects/Adjustments.h"
#include "imaging/SubjectDialog.h"
#include <QApplication>
#include <QMenuBar>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <cmath>

namespace compositor {
namespace {
QString adjustmentName(const QString& kind){
    static const char* names[]{"色相/饱和度","色阶","曲线","曝光","渐变映射","颗粒","反相","黑白","色彩平衡","高斯模糊","动感模糊","添加杂色"};
const QStringList kinds{"Hue/Saturation","Levels","Curves","Exposure","Gradient Map","Grain","Invert","Black & White","Color Balance","Gaussian Blur","Motion Blur","Add Noise"};
    const int index=int(std::find(kinds.begin(),kinds.end(),kind)-kinds.begin());
    return index>=0&&index<12?QString::fromUtf8(names[index]):kind;
}
}
void MainWindow::setupAdjustmentActions(){
    auto*menu=menuBar()->addMenu("调整");
    auto*live=menu->addMenu("新建调整图层");
    for(const QString kind:{"Hue/Saturation","Levels","Curves","Exposure","Gradient Map","Grain","Invert","Black & White","Color Balance","Gaussian Blur","Motion Blur","Add Noise"}){
        const QString label=adjustmentName(kind);
        action(menu,label+"…",{},[this,kind]{adjust(kind,false);});
        action(live,label+"…",{},[this,kind]{adjust(kind,true);});
    }
    action(menu,"编辑调整图层…",{},[this]{if(active()&&!active()->adjustmentJson.empty())adjust({},true,true);});
    auto*filters=menuBar()->addMenu("滤镜");
    const struct {const char* name;int kind;} filterItems[]{{"高斯模糊",0},{"动感模糊",1},{"添加杂色",2},{"镜头校正",3},{"Camera Raw 滤镜",10},{"内容识别填充",4},{"暗角",5},{"泛光",6},{"色调对比",7},{"抖动",8},{"扫描线",9}};
    for(const auto& item:filterItems)action(filters,QString(item.name)+"…",{},[this,kind=item.kind]{runFilter(kind);});
    action(filters,"移除背景…",{},[this]{removeBackground();});
}
void MainWindow::adjust(const QString&kind,bool live,bool existing){
    auto*p=current();auto*l=active();
    if(!p||!p->document||(!live&&(!l||!l->raster||l->group||!l->adjustmentJson.empty())))return;
    if(editPanel_)return;
    if(live&&!existing){
        if(p->document->layers.size()>=10000)return;
        Layer added;added.id=newId();added.name=adjustmentName(kind).toStdString();added.transform={0,0,double(p->document->width),double(p->document->height)};
        auto settings=QJsonDocument::fromJson(QByteArray::fromStdString(effects::defaultAdjustmentJson(kind.toStdString()))).object();
        if(kind=="Gradient Map"){
            auto color=[](const QColor& value){return QJsonObject{{"red",value.redF()},{"green",value.greenF()},{"blue",value.blueF()}};};
            settings["gradientMapSettings"]=QJsonObject{{"shadows",color(foreground_)},{"highlights",color(background_)},{"reversed",false}};
        }
        if(kind=="Grain")settings["grainSettings"]=QJsonObject{{"amount",25},{"size",1.5},{"roughness",50},{"seed",double(QRandomGenerator::global()->generate())}};
        added.adjustmentJson=QJsonDocument(settings).toJson(QJsonDocument::Compact).toStdString();
        if(l)added.parentId=l->group?l->id:l->parentId;
        const auto at=std::find_if(p->document->layers.begin(),p->document->layers.end(),[&](const Layer& value){return value.id==p->active;});
        const auto index=at==p->document->layers.end()?p->document->layers.size():size_t(at-p->document->layers.begin()+1);
        auto selected=std::vector<std::string>{added.id};auto collapsed=p->collapsedGroups;collapsed.erase(added.parentId);
        const auto name="新建"+adjustmentName(kind).toStdString()+"调整";
        edit(name.c_str(),[&](Document& document){document.layers.insert(document.layers.begin()+index,added);p->active=added.id;});
        p->selected=std::move(selected);p->collapsedGroups=std::move(collapsed);p->maskSelected=false;existing=true;
    }
    const auto before=*p->document;const auto id=p->active;
    AdjustmentDialogOptions options;
    if(!live){options.initialAdjustmentJson=p->toolState.filterSettings.beginAdjustment(kind,foreground_,background_);options.onApply=[p](const std::string& json){p->toolState.filterSettings.rememberAdjustment(json);};}
    const auto title=live?adjustmentName(QJsonDocument::fromJson(QByteArray::fromStdString(active()->adjustmentJson)).object()["kind"].toString()):adjustmentName(kind);
    auto host=makeEditPanelHost(*p,before,live?"编辑"+title.toStdString()+"调整":title.toStdString());
    try{editPanel_=openAdjustmentPanel(this,before,id,kind,live,existing,options,std::move(host));}
    catch(...){if(live)if(auto snapshot=p->history.cancel()){p->document=std::move(snapshot->document);p->active=std::move(snapshot->activeLayer);}throw;}
    refresh(false,false);
}
void MainWindow::removeBackground(){
    auto*p=current();auto*l=active();if(editPanel_||!p||!p->document||!l||!l->raster||l->group||!l->adjustmentJson.empty())return;
    const auto before=*p->document;const auto original=*l;
    auto model=QDir(QApplication::applicationDirPath()).filePath("models/birefnet-lite.onnx");
    if(!QFileInfo::exists(model))model=QStringLiteral(COMPOSITOR_SOURCE_ROOT)+"/dependencies/imaging/model/birefnet-lite.onnx";
    imaging::SubjectDialogOptions options;options.initial=p->toolState.filterSettings.background;
    options.onApply=[p](const imaging::MatteSettings& settings){p->toolState.filterSettings.background=settings;};
    auto host=makeEditPanelHost(*p,before,"移除背景");
    editPanel_=openSubjectPanel(this,before,original,std::filesystem::path(model.toStdWString()),options,std::move(host));refresh(false,false);
}
}
