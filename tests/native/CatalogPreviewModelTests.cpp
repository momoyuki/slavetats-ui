#include "native/CatalogPreviewModel.h"
#include <iostream>
#include <stdexcept>
using namespace stui::native;
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main() {
 try {
  CatalogPreviewModel m;
  check(m.begin({0x14,3,stui::core::TattooArea::body,2},true),"begin");
  stui::core::TattooEntry a,b;
  a.name="A";a.glow=0x123456;a.glossiness=7;a.specularStrength=0;a.bump="pack_n.dds";a.emissiveMult=2.5F;
  b.name="B";b.glossiness=9;b.specularStrength=11;
  check(m.select(a),"click selects A");
  auto paint=m.takePaint();check(paint&&paint->candidate->name=="A","click paints A");
  check(m.displayCandidate()&&m.displayCandidate()->name=="A"&&
    m.selectedCandidate(),"click displays the selected candidate");
  check(!m.saveCandidate(),"pending selection cannot save");
  m.completePaint(*paint,true);check(m.saveCandidate()->name=="A","completed selection can save");
  check(m.requestRepaint(),"active preview can be refreshed after a completed paint");
  paint=m.takePaint();check(paint&&paint->candidate&&paint->candidate->name=="A",
    "refresh repeats the same transient candidate");
  check(!m.requestRepaint(),"refresh must not duplicate an in-flight paint");
  m.completePaint(*paint,true);check(m.saveCandidate()->name=="A",
    "refresh completion preserves pinned Save intent");
  m.setInk(0xABCDEF);m.setGlossy(true);
  check(m.displayCandidate()&&m.displayCandidate()->color==0xABCDEF,
    "quick controls display the active candidate");
  paint=m.takePaint();check(paint&&paint->candidate->color==0xABCDEF&&paint->candidate->glossiness==7&&paint->candidate->specularStrength==5,"quick merge");
  m.completePaint(*paint,true);
  m.select(b);paint=m.takePaint();check(paint&&paint->candidate->glossiness==9&&paint->candidate->specularStrength==11,"candidate glossy strengths");
  check(!m.saveCandidate(),"clicked B cannot save before paint completes");
  m.completePaint(*paint,true);check(m.saveCandidate()->name=="B"&&m.saveCandidate()->color==0xABCDEF,"latest click keeps quick values");
  m.select(a);paint=m.takePaint();m.undo();m.completePaint(*paint,true);
  check(!m.saveCandidate(),"late selection cannot repin after Undo");
  paint=m.takePaint();check(paint&&!paint->candidate,"Undo restores original");
  m.completePaint(*paint,false);check(m.restorePending(),"failed restore remains pending");
  m.retry();paint=m.takePaint();check(paint&&!paint->candidate,"retry preserves restore");
  m.completePaint(*paint,true);check(!m.restorePending(),"successful rollback clears obligation");
  m.select(a);paint=m.takePaint();m.close();m.completePaint(*paint,true);
  check(m.open(),"close must wait for physical cleanup");
  paint=m.takePaint();check(paint&&!paint->candidate,"close forces restoration");
  m.completePaint(*paint,true);check(!m.open(),"close after cleanup");
  check(m.begin({0x14,4,stui::core::TattooArea::body,2},true),"untouched session opens");
  m.close();
  check(m.open(),"untouched close waits for runtime to end session");
  paint=m.takePaint();check(paint&&!paint->candidate,"untouched close emits end barrier");
  m.completePaint(*paint,true);check(!m.open(),"untouched session ends after runtime ack");
  check(!m.begin({0,4,stui::core::TattooArea::body,2},true),"never default actor");
  check(!m.begin({0x14,4,stui::core::TattooArea::body,2},false),"unavailable backend");
  check(!m.begin({0x14,4,static_cast<stui::core::TattooArea>(99),2},true),"invalid area");
  check(m.begin({0x14,5,stui::core::TattooArea::body,2},true),"fresh session");
  m.setInk(0x112233);m.setGlow(0xFF0000);m.setGlossy(false);
  check(!m.saveCandidate()&&!m.takePaint(),"unselected controls stay local");
  m.select(a);paint=m.takePaint();
  check(paint&&paint->candidate->glow==0xFF0000&&paint->candidate->bump=="pack_n.dds"&&
    paint->candidate->emissiveMult==2.5F&&paint->candidate->glossiness==0,"quick fields preserve metadata");
  auto foreign=*paint;foreign.context.actorFormId=0x123;
  m.completePaint(foreign,true);check(!m.saveCandidate(),"foreign completion rejected");
  m.completePaint(*paint,false);check(!m.saveCandidate()&&!m.takePaint(),"failed paint waits for retry");
  m.retry();paint=m.takePaint();m.completePaint(*paint,true);
  check(m.saveCandidate()->color==0x112233,"retry keeps exact selection");
  m.undo();paint=m.takePaint();m.completePaint(*paint,true);
  m.select(a);paint=m.takePaint();m.completePaint(*paint,true);
  check(m.saveCandidate()->glow==a.glow&&m.saveCandidate()->glossiness==a.glossiness,"Undo clears quick overrides");
  m.select(b);paint=m.takePaint();m.completePaint(*paint,false);m.close();
  check(m.open()&&m.restorePending(),"partial failed paint still requires rollback on Close");
  paint=m.takePaint();const auto oldTicket=*paint;m.completePaint(*paint,true);
  check(m.begin({0x14,6,stui::core::TattooArea::body,2},true),"open after cleanup");
  m.select(a);paint=m.takePaint();m.completePaint(oldTicket,true);
  check(!m.saveCandidate(),"old-session completion cannot acknowledge new paint");
  m.completePaint(*paint,true);check(m.saveCandidate()->name=="A","new-session paint completes normally");
  check(m.select(b),"click B before next frame");
  paint=m.takePaint();check(paint&&paint->candidate->name=="B","B paint begins");
  check(m.select(a),"click A while B paint is in flight");
  m.completePaint(*paint,true);
  check(!m.saveCandidate(),"stale B paint cannot acknowledge latest A selection");
  paint=m.takePaint();check(paint&&paint->candidate->name=="A","latest selection repaints A");
  m.completePaint(*paint,true);check(m.saveCandidate()->name=="A","only latest paint enables Save");
  std::cout<<"PASS catalog click selection and rollback model\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
