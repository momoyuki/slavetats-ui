#include "native/SkyrimBrowserCameraPort.h"
#include "native/CameraControlLease.h"
#include "native/BrowserCameraPose.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <cmath>
#include <mutex>

#define SMOOTHCAM_API_COMMONLIB
#include "SmoothCamAPI.h"

namespace stui::native {


namespace {
std::optional<BrowserCameraView> captureRenderedView(RE::PlayerCamera* camera) {
    auto* rendered = RE::Main::WorldRootCamera();
    if (!camera || !camera->currentState || !rendered) return std::nullopt;
    RE::NiPoint3 origin, direction;
    if (!rendered->WindowPointToRay(512,512,origin,direction,1024,1024))
        return std::nullopt;
    const auto position = rendered->world.translate;
    return browserCameraViewFromForward({position.x,position.y,position.z},
        {direction.x,direction.y,direction.z});
}
BrowserCameraLandmarks captureLandmarks(RE::Actor* actor) {
    BrowserCameraLandmarks result;
    auto* root = actor ? actor->Get3D() : nullptr;
    if (!root) return result;
    const auto center = root->worldBound.center;
    result.boundsCenter = CameraVector{center.x,center.y,center.z};
    result.boundsRadius = root->worldBound.radius;
    const auto bone = [root](const char* name) -> std::optional<CameraVector> {
        const auto* node = root->GetObjectByName(RE::BSFixedString(name));
        if (!node) return std::nullopt;
        const auto p = node->world.translate;
        return CameraVector{p.x,p.y,p.z};
    };
    result.head=bone("NPC Head [Head]");
    result.torso=bone("NPC Spine2 [Spn2]");
    result.leftHand=bone("NPC L Hand [LHnd]");
    result.rightHand=bone("NPC R Hand [RHnd]");
    result.leftFoot=bone("NPC L Foot [Lft ]");
    result.rightFoot=bone("NPC R Foot [Rft ]");
    return result;
}
}  // namespace

struct SkyrimBrowserCameraPort::Impl {
    SmoothCamAPI::IVSmoothCam1* smoothCam{};
    int smoothCamVersion{};
    std::unique_ptr<CameraControlLease> lease;
    bool active{}, menuOnRight{true};
    std::uint32_t formId{}, openingFormId{};
    RE::BSTSmartPointer<RE::TESCameraState> savedState, freeStateOwner;
    RE::FreeCameraState* freeState{};
    RE::NiPointer<RE::NiNode> root;
    RE::NiPointer<RE::NiCamera> rendered;
    RE::ActorHandle previousTarget;
    std::optional<BrowserCameraControlSnapshot<RE::ThirdPersonState>> savedThirdControls;
    RE::NiPoint3 savedFreeTranslation;
    RE::BSTPoint2<float> savedFreeRotation, savedFreeUpDown;
    std::int16_t savedVertical{};
    bool savedRun{}, savedLock{}, savedFreeInput{}, savedBlockInput{}, inputCaptured{};
    std::optional<BrowserCameraView> opening;
    BrowserCameraView view;
    CameraVector pivot{}, openingPivot{}, baseFocus{};
    std::mutex currentPanMutex;
    std::optional<BrowserCameraPan> currentPan;
    BrowserCameraRecallStore recall;
    bool recalledOpening{};
    unsigned traceUpdates{};
};

SkyrimBrowserCameraPort::SkyrimBrowserCameraPort() : m_impl(std::make_unique<Impl>()) {}
SkyrimBrowserCameraPort::~SkyrimBrowserCameraPort() = default;

void SkyrimBrowserCameraPort::setSmoothCamInterface(
    void* interfaceInstance, int version) noexcept {
    if (m_impl->active || (m_impl->lease && m_impl->lease->held())) return;
    m_impl->smoothCam = static_cast<SmoothCamAPI::IVSmoothCam1*>(interfaceInstance);
    m_impl->smoothCamVersion = version;
    if (!m_impl->smoothCam) {
        m_impl->lease.reset();
        return;
    }
    try {
    m_impl->lease = std::make_unique<CameraControlLease>(
        [this] {
            const auto result = m_impl->smoothCam->RequestCameraControl(SKSE::GetPluginHandle());
            SKSE::log::info("[CameraTrace] SmoothCam ownership result={}", static_cast<int>(result));
            if (result == SmoothCamAPI::APIResult::OK) return CameraControlResult::granted;
            if (result == SmoothCamAPI::APIResult::AlreadyGiven) {
                return CameraControlResult::alreadyOwned;
            }
            return CameraControlResult::denied;
        },
        [this] {
            (void)m_impl->smoothCam->ReleaseCameraControl(SKSE::GetPluginHandle());
        },
        [this] {
            if (m_impl->smoothCamVersion >= 2) {
                auto* v2 = static_cast<SmoothCamAPI::IVSmoothCam2*>(m_impl->smoothCam);
                (void)v2->SendToGoalPosition(
                    SKSE::GetPluginHandle(), true, false,
                    RE::PlayerCharacter::GetSingleton());
            }
        });
    } catch (...) {
        m_impl->smoothCam = nullptr;
        m_impl->lease.reset();
    }
}

bool SkyrimBrowserCameraPort::isTargetLoaded(std::uint32_t formId) {
    auto* actor = RE::TESForm::LookupByID<RE::Actor>(formId);
    return actor && actor->Is3DLoaded();
}


void SkyrimBrowserCameraPort::rememberView(std::uint32_t formId, bool menuOnRight) {
    m_impl->opening.reset();
    m_impl->openingFormId=formId;
    m_impl->menuOnRight=menuOnRight;
    if (formId != 0x14) return;
    auto* actor=RE::TESForm::LookupByID<RE::Actor>(formId);
    const auto focus=resolveBrowserCameraFocus(captureLandmarks(actor),BrowserCameraFocus::wholeBody);
    if (!actor || !actor->Is3DLoaded() || !focus) return;
    m_impl->opening=captureRenderedView(RE::PlayerCamera::GetSingleton());
    m_impl->openingPivot=*focus;
}

bool SkyrimBrowserCameraPort::begin(std::uint32_t formId, bool menuOnRight) {
    auto* actor=RE::TESForm::LookupByID<RE::Actor>(formId);
    auto* camera=RE::PlayerCamera::GetSingleton();
    if (REL::Module::IsVR() || formId != 0x14 || !actor || !actor->Is3DLoaded() ||
        !camera || !camera->cameraRoot || !camera->currentState ||
        camera->currentState->id != RE::CameraState::kThirdPerson || m_impl->active) return false;
    const auto captured=captureRenderedView(camera);
    const auto focus=resolveBrowserCameraFocus(captureLandmarks(actor),BrowserCameraFocus::wholeBody);
    auto heldFree=camera->GetRuntimeData().cameraStates[RE::CameraState::kFree];
    if (!captured || !focus || !heldFree) return false;
    if (m_impl->lease && !m_impl->lease->acquire()) return false;

    m_impl->savedState=camera->currentState;
    m_impl->freeStateOwner=heldFree;
    m_impl->freeState=static_cast<RE::FreeCameraState*>(heldFree.get());
    auto* free=m_impl->freeState;
    m_impl->savedFreeTranslation=free->translation;
    m_impl->savedFreeRotation=free->rotation;
    m_impl->savedFreeUpDown=free->zUpDown;
    m_impl->savedVertical=free->verticalDirection;
    m_impl->savedRun=free->useRunSpeed;
    m_impl->savedLock=free->lockToZPlane;
    m_impl->savedFreeInput=free->IsInputEventHandlingEnabled();
    m_impl->savedThirdControls=BrowserCameraControlSnapshot<RE::ThirdPersonState>(
        *static_cast<RE::ThirdPersonState*>(m_impl->savedState.get()));
    m_impl->previousTarget=camera->cameraTarget;
    m_impl->formId=formId;
    m_impl->menuOnRight=menuOnRight;
    m_impl->view=*captured;
    m_impl->pivot=*focus;
    m_impl->baseFocus=*focus;
    m_impl->recalledOpening=false;
    if (const auto* actorRoot=actor->Get3D()) {
        const auto p=actorRoot->world.translate;
        if (const auto recalled=m_impl->recall.recall(menuOnRight,{p.x,p.y,p.z})) {
            m_impl->view=recalled->view;
            m_impl->pivot=recalled->pivot;
            m_impl->recalledOpening=true;
        }
    }
    if (!m_impl->opening || m_impl->openingFormId != formId) {
        m_impl->opening=*captured;
        m_impl->openingPivot=*focus;
        m_impl->openingFormId=formId;
    }

    camera->ToggleFreeCameraMode(false);
    m_impl->active=camera->currentState.get()==heldFree.get();
    if (!m_impl->active) { restore(); return false; }
    m_impl->root=camera->cameraRoot;
    m_impl->rendered=RE::NiPointer<RE::NiCamera>{RE::Main::WorldRootCamera()};
    if (!m_impl->root || !m_impl->rendered) { restore(); return false; }
    free->SetInputEventHandlingEnabled(false);
    if (auto* controls=RE::PlayerControls::GetSingleton()) {
        m_impl->savedBlockInput=controls->blockPlayerInput;
        m_impl->inputCaptured=true;
        controls->blockPlayerInput=true;
    }
    free->translation={m_impl->view.position[0],m_impl->view.position[1],
        m_impl->view.position[2]};
    free->rotation.x=m_impl->view.pitch;
    free->rotation.y=m_impl->view.yaw;
    free->zUpDown={0,0};
    free->verticalDirection=0;
    m_impl->traceUpdates=0;
    SKSE::log::info("[CameraTrace] free-camera-v1 acquired position=({:.2f},{:.2f},{:.2f}) yaw={:.3f} pitch={:.3f}",
        captured->position[0],captured->position[1],captured->position[2],captured->yaw,captured->pitch);
    return true;
}

bool SkyrimBrowserCameraPort::control(const BrowserCameraMotion& motion) {
    auto* camera=RE::PlayerCamera::GetSingleton();
    if (!m_impl->active) return false;
    if (!isTargetLoaded(m_impl->formId) || !camera ||
        camera->currentState.get()!=m_impl->freeStateOwner.get() ||
        camera->cameraRoot!=m_impl->root || RE::Main::WorldRootCamera()!=m_impl->rendered.get() ||
        camera->cameraTarget!=m_impl->previousTarget) {
        SKSE::log::info("[CameraTrace] free-camera ownership invalidated");
        restore(); return false;
    }
    if (motion.initialFrame && m_impl->recalledOpening) {
        // The remembered view replaces only the automatic opening composition.
    } else if (motion.reset && m_impl->opening) {
        m_impl->view=*m_impl->opening;
        m_impl->pivot=m_impl->openingPivot;
        m_impl->baseFocus=m_impl->openingPivot;
    } else if (motion.focus!=BrowserCameraFocus::none) {
        auto* actor=RE::TESForm::LookupByID<RE::Actor>(m_impl->formId);
        if (const auto focus=resolveBrowserCameraFocus(captureLandmarks(actor),motion.focus)) {
            m_impl->pivot=*focus;
            m_impl->baseFocus=*focus;
            const auto& p=m_impl->view.position;
            const float dx=p[0]-(*focus)[0],dy=p[1]-(*focus)[1],dz=p[2]-(*focus)[2];
            const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
            RE::NiPoint3 origin,direction;
            float lateral=0;
            const float screenX=std::isfinite(motion.frameCenterX)
                ? std::clamp(motion.frameCenterX,0.05F,0.95F) : (m_impl->menuOnRight ? 0.25F : 0.75F);
            if (m_impl->rendered->WindowPointToRay(static_cast<int>(screenX*1024),512,
                    origin,direction,1024,1024)) {
                const float length=direction.Length();
                if (length>0) lateral=distance*(direction.x*std::cos(m_impl->view.yaw)-
                    direction.y*std::sin(m_impl->view.yaw))/length;
            }
            m_impl->view=centerBrowserCameraView(m_impl->view,*focus,lateral);
        }
    } else {
        applyBrowserCameraMotion(m_impl->view,m_impl->pivot,motion);
    }
    if (motion.initialFrame && !m_impl->recalledOpening) {
        (void)applyBrowserCameraPan(m_impl->view, m_impl->pivot,
            {motion.initialPanX, motion.initialPanY});
    }
    if (const auto pan = browserCameraPanFromFocus(
            m_impl->view, m_impl->pivot, m_impl->baseFocus)) {
        const std::scoped_lock lock(m_impl->currentPanMutex);
        m_impl->currentPan = pan;
    }
    auto* free=m_impl->freeState;
    free->SetInputEventHandlingEnabled(false);
    free->zUpDown={0,0};
    free->verticalDirection=0;
    free->translation={m_impl->view.position[0],m_impl->view.position[1],m_impl->view.position[2]};
    free->rotation.x=m_impl->view.pitch;
    free->rotation.y=m_impl->view.yaw;
    camera->rotationInput={0,0};
    camera->translationInput={0,0,0};
    camera->zoomInput=0;
    if (auto* controls=RE::PlayerControls::GetSingleton()) controls->blockPlayerInput=true;
    if (auto* ui=RE::UI::GetSingleton(); ui && ui->GameIsPaused()) camera->Update();
    if (++m_impl->traceUpdates<=3 || m_impl->traceUpdates%60==0) {
        const auto actual=m_impl->rendered->world.translate;
        SKSE::log::info("[CameraTrace] free-camera-v1 wanted=({:.2f},{:.2f},{:.2f}) actual=({:.2f},{:.2f},{:.2f}) pivot=({:.2f},{:.2f},{:.2f})",
            free->translation.x,free->translation.y,free->translation.z,actual.x,actual.y,actual.z,
            m_impl->pivot[0],m_impl->pivot[1],m_impl->pivot[2]);
    }
    return true;
}

void SkyrimBrowserCameraPort::restore() {
    auto* camera=RE::PlayerCamera::GetSingleton();
    const bool owns=m_impl->active && camera &&
        camera->currentState.get()==m_impl->freeStateOwner.get() &&
        camera->cameraRoot==m_impl->root &&
        RE::Main::WorldRootCamera()==m_impl->rendered.get() &&
        camera->cameraTarget==m_impl->previousTarget;
    if (owns && m_impl->formId == 0x14 && isTargetLoaded(m_impl->formId)) {
        if (auto* actor=RE::TESForm::LookupByID<RE::Actor>(m_impl->formId)) {
            if (const auto* actorRoot=actor->Get3D()) {
                const auto p=actorRoot->world.translate;
                (void)m_impl->recall.rememberObserved(m_impl->menuOnRight,
                    captureRenderedView(camera),m_impl->pivot,{p.x,p.y,p.z});
            }
        }
    }
    if (owns) {
        camera->ToggleFreeCameraMode(false);
        if (m_impl->savedState && camera->currentState!=m_impl->savedState)
            camera->SetState(m_impl->savedState.get());
    }
    if (owns && m_impl->savedState && m_impl->savedThirdControls)
        m_impl->savedThirdControls->restore(
            *static_cast<RE::ThirdPersonState*>(m_impl->savedState.get()));
    if (m_impl->freeStateOwner &&
        (owns || !camera || camera->currentState.get()!=m_impl->freeStateOwner.get())) {
        auto* free=m_impl->freeState;
        free->translation=m_impl->savedFreeTranslation;
        free->rotation=m_impl->savedFreeRotation;
        free->zUpDown=m_impl->savedFreeUpDown;
        free->verticalDirection=m_impl->savedVertical;
        free->useRunSpeed=m_impl->savedRun;
        free->lockToZPlane=m_impl->savedLock;
        free->SetInputEventHandlingEnabled(m_impl->savedFreeInput);
    }
    else if (m_impl->freeStateOwner) {
        // Release our input suppression without overwriting a replacement view.
        m_impl->freeState->SetInputEventHandlingEnabled(m_impl->savedFreeInput);
    }
    if (m_impl->inputCaptured) {
        if (auto* controls=RE::PlayerControls::GetSingleton())
            controls->blockPlayerInput=m_impl->savedBlockInput;
    }
    m_impl->inputCaptured=false;
    m_impl->active=false;
    {
        const std::scoped_lock lock(m_impl->currentPanMutex);
        m_impl->currentPan.reset();
    }
    if (m_impl->lease) m_impl->lease->release();
    m_impl->savedState.reset();
    m_impl->freeStateOwner.reset();
    m_impl->freeState=nullptr;
    m_impl->savedThirdControls.reset();
    m_impl->root=nullptr;
    m_impl->rendered=nullptr;
}
void SkyrimBrowserCameraPort::clearRememberedViews() {
    m_impl->recall.clear();
}
std::optional<BrowserCameraPan> SkyrimBrowserCameraPort::currentPan() const noexcept {
    const std::scoped_lock lock(m_impl->currentPanMutex);
    return m_impl->currentPan;
}
}  // namespace stui::native
