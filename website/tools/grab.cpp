// Fernsteuerung+Screenshot fuer Qt-Programme per DYLD_INSERT_LIBRARIES.
// Liest Kommandos zeilenweise aus $GRAB_CMD, schreibt Antworten nach $GRAB_OUT.
#include <QtCore>
#include <QtWidgets>
#include <cstdio>
static QFile *outf;
static void say(const QString &s){ outf->write(s.toUtf8()+"\n"); outf->flush(); }
static QString clean(QString s){ return s.replace('&',"").replace('\n',' '); }
static QWidgetList wins(){ QWidgetList r; for(auto*w:QApplication::topLevelWidgets()) if(w->isVisible()) r<<w; return r; }
static QWidget* win(const QString &t){ auto l=wins(); if(t.isEmpty()) { if(auto*a=QApplication::activeModalWidget()) return a; return l.value(0);} for(auto*w:l) if(w->windowTitle().contains(t,Qt::CaseInsensitive)||QString(w->metaObject()->className()).contains(t,Qt::CaseInsensitive)) return w; return nullptr; }
static void dump(QWidget*w){ say(QString("WIN %1 '%2' %3x%4").arg(w->metaObject()->className(),w->windowTitle()).arg(w->width()).arg(w->height()));
 int i=0; for(auto*c:w->findChildren<QWidget*>()){ if(!c->isVisible()) continue; QString k=c->metaObject()->className(); QString v;
  if(auto*b=qobject_cast<QAbstractButton*>(c)) v="btn:"+clean(b->text())+(b->isCheckable()?(b->isChecked()?" [x]":" [ ]"):"");
  else if(auto*e=qobject_cast<QLineEdit*>(c)) v=QString("edit#%1:%2 ph=%3").arg(w->findChildren<QLineEdit*>().indexOf(e)).arg(e->echoMode()==QLineEdit::Password?"***":e->text(),e->placeholderText());
  else if(auto*cb=qobject_cast<QComboBox*>(c)) v=QString("combo#%1:%2").arg(w->findChildren<QComboBox*>().indexOf(cb)).arg(cb->currentText());
  else if(auto*iv=qobject_cast<QAbstractItemView*>(c)) { v=QString("view#%1 rows=%2").arg(w->findChildren<QAbstractItemView*>().indexOf(iv)).arg(iv->model()?iv->model()->rowCount(iv->rootIndex()):-1); for(int r=0;iv->model()&&r<iv->model()->rowCount(iv->rootIndex())&&r<8;r++) v+=" | "+iv->model()->index(r,0,iv->rootIndex()).data().toString(); }
  else if(auto*l=qobject_cast<QLabel*>(c)) v="label:"+clean(l->text()).left(70);
  else if(auto*tb=qobject_cast<QTabBar*>(c)){ v="tabs:"; for(int t=0;t<tb->count();t++) v+=clean(tb->tabText(t))+"|"; }
  if(!v.isEmpty()) say("  "+v); i++; }
 for(auto*a:w->findChildren<QAction*>()) if(!a->text().isEmpty()&&!a->isSeparator()) say("  act:"+clean(a->text())+(a->isEnabled()?"":" (aus)"));
}
static void exec(const QString &line){ QString cmd=line.section(' ',0,0), rest=line.section(' ',1);
 if(cmd=="list"){ for(auto*w:wins()) dump(w); say("--ende"); }
 else if(cmd=="shot"){ QString path=rest.section(' ',0,0), t=rest.section(' ',1); auto*w=win(t); if(!w){say("keinfenster "+t);return;} w->raise(); QCoreApplication::processEvents(); QPixmap p=w->grab(); p.setDevicePixelRatio(1); say(QString("shot %1 %2 %3").arg(path).arg(p.save(path)).arg(w->windowTitle())); }
 else if(cmd=="shotw"){ // shotw pfad klasse  -> einzelnes Widget
   QString path=rest.section(' ',0,0), k=rest.section(' ',1); for(auto*w:QApplication::allWidgets()) if(w->isVisible()&&k==w->metaObject()->className()){ say(QString("shot %1 %2").arg(path).arg(w->grab().save(path))); return;} say("nicht gefunden"); }
 else if(cmd=="click"){ auto*w=win(""); for(auto*tl:wins()) for(auto*b:tl->findChildren<QAbstractButton*>()) if(b->isVisible()&&clean(b->text()).contains(rest,Qt::CaseInsensitive)){ say("click "+clean(b->text())); b->click(); return;} say("kein knopf "+rest); Q_UNUSED(w); }
 else if(cmd=="action"){ for(auto*o:QApplication::allWidgets()) for(auto*a:o->findChildren<QAction*>()) if(clean(a->text()).contains(rest,Qt::CaseInsensitive)&&!a->menu()){ say("action "+clean(a->text())); QTimer::singleShot(0,a,&QAction::trigger); return;} for(auto*o:qApp->findChildren<QAction*>()) if(clean(o->text()).contains(rest,Qt::CaseInsensitive)){ say("action "+clean(o->text())); QTimer::singleShot(0,o,&QAction::trigger); return;} say("keine aktion "+rest); }
 else if(cmd=="set"){ int n=rest.section(' ',0,0).toInt(); QString v=rest.section(' ',1); auto*w=win(""); auto l=w->findChildren<QLineEdit*>(); if(n<l.size()){ l[n]->setFocus(); l[n]->setText(v); emit l[n]->textEdited(v); say("set ok"); } else say("kein edit"); }
 else if(cmd=="return"){ int n=rest.toInt(); auto*w=win(""); auto l=w->findChildren<QLineEdit*>(); if(n<l.size()) { emit l[n]->returnPressed(); say("return ok"); } }
 else if(cmd=="next"){ if(auto*wz=qobject_cast<QWizard*>(win(""))){ wz->next(); say("next"); } else say("kein wizard"); }
 else if(cmd=="check"){ for(auto*tl:wins()) for(auto*b:tl->findChildren<QAbstractButton*>()) if(clean(b->text()).contains(rest,Qt::CaseInsensitive)){ b->setChecked(!b->isChecked()); say("toggle"); return;} }
 else if(cmd=="row"){ // row view# zeile -> markieren
   auto*w=win(""); int v=rest.section(' ',0,0).toInt(), r=rest.section(' ',1,1).toInt(); auto l=w->findChildren<QAbstractItemView*>(); if(v<l.size()){ auto ix=l[v]->model()->index(r,0,l[v]->rootIndex()); l[v]->setCurrentIndex(ix); say("row "+ix.data().toString()); } }
 else if(cmd=="open"){ auto*w=win(""); int v=rest.section(' ',0,0).toInt(), r=rest.section(' ',1,1).toInt(); auto l=w->findChildren<QAbstractItemView*>(); if(v<l.size()){ auto ix=l[v]->model()->index(r,0,l[v]->rootIndex()); l[v]->setCurrentIndex(ix); emit l[v]->doubleClicked(ix); emit l[v]->activated(ix); say("open "+ix.data().toString()); } }
 else if(cmd=="tab"){ int n=rest.toInt(); auto*w=win(""); for(auto*t:w->findChildren<QTabWidget*>()) if(t->isVisible()){ t->setCurrentIndex(n); say("tab"); return;} for(auto*t:w->findChildren<QTabBar*>()) if(t->isVisible()){ t->setCurrentIndex(n); say("tab"); return;} }
 else if(cmd=="dock"){ auto*w=win(""); for(auto*d:w->findChildren<QDockWidget*>()) say("dock "+d->windowTitle()+(d->isVisible()?" sichtbar":"")); }
 else if(cmd=="resize"){ auto*w=win(""); w->resize(rest.section(' ',0,0).toInt(),rest.section(' ',1,1).toInt()); say("resize"); }
 else if(cmd=="shotmenu"){ QString path=rest.section(' ',0,0), k=rest.section(' ',1); for(auto*w:QApplication::allWidgets()) if(auto*m=qobject_cast<QMenu*>(w)) for(auto*a:m->actions()) if(clean(a->text()).contains(k,Qt::CaseInsensitive)){ m->ensurePolished(); m->adjustSize(); say(QString("shotmenu %1").arg(m->grab().save(path))); return;} say("kein menue"); }
 else if(cmd=="setlabel"){ QString k=rest.section('=',0,0), v=rest.section('=',1); for(auto*tl:wins()) for(auto*l:tl->findChildren<QLabel*>()) if(l->text().contains(k)){ l->setText(v); say("label ok"); return;} say("kein label"); }
 else if(cmd=="seted"){ QString k=rest.section('=',0,0), v=rest.section('=',1); for(auto*tl:wins()) for(auto*l:tl->findChildren<QLineEdit*>()) if(l->text().contains(k)){ l->setText(v); say("edit ok"); return;} say("kein edit"); }
 else if(cmd=="replace"){ QString k=rest.section('=',0,0), v=rest.section('=',1); int n=0; for(auto*tl:wins()) { for(auto*e:tl->findChildren<QPlainTextEdit*>()){ QString t=e->toPlainText(); if(e->isVisible()&&t.contains(k)){ e->setPlainText(t.replace(k,v)); n++; } } for(auto*e:tl->findChildren<QTextEdit*>()){ QString t=e->toPlainText(); if(e->isVisible()&&t.contains(k)){ e->setPlainText(t.replace(k,v)); n++; } } } say(QString("replace %1").arg(n)); }
 else if(cmd=="tabtext"){ for(auto*tl:wins()) for(auto*t:tl->findChildren<QTabBar*>()) for(int i=0;i<t->count();i++) if(clean(t->tabText(i))==rest){ t->setCurrentIndex(i); say("tabtext ok"); return;} say("kein tab"); }
 else if(cmd=="quit"){ qApp->quit(); }
}
static qint64 off=0;
static void poll(){ QFile f(qEnvironmentVariable("GRAB_CMD")); if(!f.open(QIODevice::ReadOnly)) return; f.seek(off); QByteArray d=f.readAll(); int e=d.lastIndexOf('\n'); if(e<0) return; off+=e+1; for(auto&l:d.left(e).split('\n')){ QString t=QString::fromUtf8(l).trimmed(); if(!t.isEmpty()) exec(t);} }
static void boot(){ QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,qEnvironmentVariable("HOME")+"/ini"); outf=new QFile(qEnvironmentVariable("GRAB_OUT")); (void)outf->open(QIODevice::WriteOnly|QIODevice::Append);
 auto*t=new QTimer(qApp); QObject::connect(t,&QTimer::timeout,qApp,[]{ if(qobject_cast<QApplication*>(qApp)) poll(); }); t->start(150); }
Q_COREAPP_STARTUP_FUNCTION(boot)
