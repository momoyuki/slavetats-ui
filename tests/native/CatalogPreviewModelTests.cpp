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
  m.hover(a);auto paint=m.takePaint();check(paint&&paint->candidate->name=="A","hover paints A");
  check(m.displayCandidate()&&m.displayCandidate()->name=="A"&&
    !m.pinnedCandidate(),"hover shows quick-control values before selection");
  m.pin();check(!m.saveCandidate(),"pending pin cannot save");
  m.completePaint(*paint,true);check(m.saveCandidate()->name=="A","completed pin can save");
  m.setInk(0xABCDEF);m.setGlossy(true);
  check(m.displayCandidate()&&m.displayCandidate()->color==0xABCDEF,
    "quick controls display the active candidate");
  paint=m.takePaint();check(paint&&paint->candidate->color==0xABCDEF&&paint->candidate->glossiness==7&&paint->candidate->specularStrength==5,"quick merge");
  m.completePaint(*paint,true);
  m.hover(b);paint=m.takePaint();check(paint&&paint->candidate->glossiness==9&&paint->candidate->specularStrength==11,"candidate glossy strengths");
  m.completePaint(*paint,true);check(!m.saveCandidate(),"hover B cannot save B or unpainted pin");
  m.hover(std::nullopt);paint=m.takePaint();check(paint&&paint->candidate->name=="A","exit restores pin");
  m.completePaint(*paint,true);check(m.saveCandidate()->name=="A"&&m.saveCandidate()->color==0xABCDEF,"pin keeps quick values");
  m.hover(b);paint=m.takePaint();m.undo();m.completePaint(*paint,true);
  check(!m.saveCandidate(),"late hover cannot repin after Undo");
  paint=m.takePaint();check(paint&&!paint->candidate,"Undo restores original");
  m.completePaint(*paint,false);check(m.restorePending(),"failed restore remains pending");
  m.retry();paint=m.takePaint();check(paint&&!paint->candidate,"retry preserves restore");
  m.completePaint(*paint,true);check(!m.restorePending(),"successful rollback clears obligation");
  m.hover(a);paint=m.takePaint();m.close();m.completePaint(*paint,true);
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
  m.hover(a);m.pin();paint=m.takePaint();
  check(paint&&paint->candidate->glow==0xFF0000&&paint->candidate->bump=="pack_n.dds"&&
    paint->candidate->emissiveMult==2.5F&&paint->candidate->glossiness==0,"quick fields preserve metadata");
  auto foreign=*paint;foreign.context.actorFormId=0x123;
  m.completePaint(foreign,true);check(!m.saveCandidate(),"foreign completion rejected");
  m.completePaint(*paint,false);check(!m.saveCandidate()&&!m.takePaint(),"failed paint waits for retry");
  m.retry();paint=m.takePaint();m.completePaint(*paint,true);
  check(m.saveCandidate()->color==0x112233,"retry keeps exact selection");
  m.undo();paint=m.takePaint();m.completePaint(*paint,true);
  m.hover(a);m.pin();paint=m.takePaint();m.completePaint(*paint,true);
  check(m.saveCandidate()->glow==a.glow&&m.saveCandidate()->glossiness==a.glossiness,"Undo clears quick overrides");
  m.hover(b);paint=m.takePaint();m.completePaint(*paint,false);m.close();
  check(m.open()&&m.restorePending(),"partial failed paint still requires rollback on Close");
  paint=m.takePaint();const auto oldTicket=*paint;m.completePaint(*paint,true);
  check(m.begin({0x14,6,stui::core::TattooArea::body,2},true),"open after cleanup");
  m.hover(a);m.pin();paint=m.takePaint();m.completePaint(oldTicket,true);
  check(!m.saveCandidate(),"old-session completion cannot acknowledge new paint");
  m.completePaint(*paint,true);check(m.saveCandidate()->name=="A","new-session paint completes normally");
  std::cout<<"PASS catalog hover pin and rollback model\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
