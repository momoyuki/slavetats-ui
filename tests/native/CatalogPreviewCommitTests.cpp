#include "native/CatalogPreviewModel.h"
#include <iostream>
#include <stdexcept>
using namespace stui::native;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
 int main(){try {
 stui::repository::TattooDefinition definition;
 definition.domain="custom";definition.section="Pack";definition.name="Mark";
 definition.texturePath="mark.dds";definition.area="Body";
 definition.glow=0x102030;definition.glossiness=3.0F;
 definition.specularStrength=4.0F;definition.emissiveMult=0.0F;
 definition.glowTexture="glow.dds";definition.bump="normal.dds";
 const auto candidate=catalogPreviewCandidate(definition);
 check(candidate.domain=="custom"&&candidate.texturePath=="mark.dds"&&
     candidate.glow==0x102030&&candidate.emissiveMult==0.0F&&
     candidate.glowTexture=="glow.dds"&&candidate.bump=="normal.dds",
     "catalog candidate preserves pack metadata and zero emission");
 const CatalogPreviewCommit bridge{{0x123,9,stui::core::TattooArea::body,2},1,candidate,false};
 const auto request=catalogPreviewCommitRequest(bridge,77);
 check(request.sessionId==77&&request.apply.actorFormId==0x123&&
     request.apply.slot==2&&request.apply.appearance&&
     request.apply.appearance->glow==0x102030&&
     request.apply.appearance->specularStrength==4.0F&&
     request.apply.appearance->emissiveMult==0.0F,
     "Save request preserves exact target and all advanced appearance fields");
 auto retryBridge=bridge;retryBridge.synchronizeOnly=true;
 check(catalogPreviewCommitRequest(retryBridge,77).apply.mode==
     stui::core::ApplyTattooMode::synchronizeOnly,
     "retry request must not repeat the tattoo mutation");
 CatalogPreviewModel m;
 check(m.begin({0x14,3,stui::core::TattooArea::body,2},true),"begin");
 stui::core::TattooEntry a,b;a.name="A";b.name="B";
 check(m.select(a),"click selects A");auto paint=m.takePaint();
 check(m.requestSave(),"Save can queue while selected paint is pending");
 check(!m.select(b)&&!m.takeCommit(),"new click cannot replace an accepted Save intent");
 m.completePaint(*paint,true);
 auto commit=m.takeCommit();check(commit&&commit->candidate.name=="A"&&!commit->synchronizeOnly,"Save captures selected A");
 const auto oldCommit=*commit;
 m.close();m.undo();check(!m.takePaint()&&m.open(),"authoritative commit blocks cancel/paint");
 m.completeCommit(*commit,false,true);
 check(!m.restorePending()&&!m.takePaint()&&!m.takeCommit(),"written mutation waits for synchronization retry");
 m.close();check(m.open(),"Close must not roll back written mutation");
 m.retry();commit=m.takeCommit();check(commit&&commit->synchronizeOnly,"retry only synchronizes");
 m.completeCommit(*commit,true,true);check(!m.open(),"committed session completes without rollback");
 check(m.begin({0x14,4,stui::core::TattooArea::body,2},true),"next session");
 m.select(a);paint=m.takePaint();m.completePaint(*paint,true);
 m.select(b);paint=m.takePaint();check(m.requestSave(),"Save selected B while B paint is in flight");
 check(!m.takeCommit(),"Save waits for selected B paint");
 m.completePaint(*paint,true);commit=m.takeCommit();check(commit&&commit->candidate.name=="B","commit B after selected paint");
 m.completeCommit(*commit,false,false);m.close();
 paint=m.takePaint();check(paint&&!paint->candidate,"pre-write commit failure can cancel and restore");
 m.completePaint(*paint,true);check(!m.open(),"cancel cleanup completes");
 check(m.begin({0x14,5,stui::core::TattooArea::body,2},true),"third session");
 m.select(a);paint=m.takePaint();m.completePaint(*paint,true);m.requestSave();
 commit=m.takeCommit();m.completeCommit(oldCommit,true,true);
 check(m.open()&&!m.takeCommit(),"old commit cannot finish a new session's in-flight commit");
 m.completeCommit(*commit,false,true);m.retry();commit=m.takeCommit();
 check(commit&&commit->synchronizeOnly,"first sync retry");
 m.completeCommit(*commit,false,false);m.close();m.undo();
 check(m.open()&&!m.takePaint(),"later retry failure cannot clear written-state barrier");
 m.retry();commit=m.takeCommit();check(commit&&commit->synchronizeOnly,"repeated failure remains sync-only");
 m.completeCommit(*commit,true,true);check(!m.open(),"eventual synchronization succeeds");
 check(m.begin({0x14,6,stui::core::TattooArea::body,2},true),"rapid-click session opens");
 check(m.select(a),"click A starts first paint");
 paint=m.takePaint();
 check(m.select(b),"click B supersedes in-flight A");
 check(m.requestSave(),"Save captures latest clicked B");
 m.completePaint(*paint,true);
 check(!m.takeCommit(),"stale A paint cannot commit B");
 paint=m.takePaint();check(paint&&paint->candidate&&paint->candidate->name=="B",
     "B paint follows stale A");
 m.completePaint(*paint,true);
 commit=m.takeCommit();check(commit&&commit->candidate.name=="B",
     "Save commits only the latest painted selection");
 m.completeCommit(*commit,true,true);check(!m.open(),"latest selection Save completes");
 std::cout<<"PASS catalog preview Save and synchronization barriers\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
