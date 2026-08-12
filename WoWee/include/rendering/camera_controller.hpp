#pragma once

#include "rendering/camera.hpp"
#include "core/input.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <functional>
#include <optional>

namespace wowee {
namespace rendering {

class TerrainManager;
class WMORenderer;
class M2Renderer;
class WaterRenderer;

class CameraController {
public:
    CameraController(Camera* camera);

    void update(float deltaTime);
    void processMouseMotion(const SDL_MouseMotionEvent& event);
    void processMouseButton(const SDL_MouseButtonEvent& event);
    void releaseMouseCapture();

    void setMovementSpeed(float speed) { movementSpeed = speed; }
    void setMouseSensitivity(float sensitivity) { mouseSensitivity = sensitivity; }
    float getMouseSensitivity() const { return mouseSensitivity; }
    void setInvertMouse(bool invert) { invertMouse = invert; }
    bool isInvertMouse() const { return invertMouse; }
    void setRelativeMouseEnabled(bool enabled) { relativeMouseEnabled_ = enabled; }
    bool isRelativeMouseEnabled() const { return relativeMouseEnabled_; }
    void setExtendedZoom(bool extended) { extendedZoom_ = extended; }
    bool isExtendedZoom() const { return extendedZoom_; }
    void setEnabled(bool enabled) { this->enabled = enabled; }
    void setTerrainManager(TerrainManager* tm) { terrainManager = tm; }
    void setWMORenderer(WMORenderer* wmo) { wmoRenderer = wmo; }
    void setM2Renderer(M2Renderer* m2) { m2Renderer = m2; }
    void setWaterRenderer(WaterRenderer* wr) { waterRenderer = wr; }

    void processMouseWheel(float delta);
    void setFollowTarget(glm::vec3* target);
    void setDefaultSpawn(const glm::vec3& position, float yawDeg, float pitchDeg) {
        defaultPosition = position;
        defaultYaw = yawDeg;
        defaultPitch = pitchDeg;
    }

    void reset();
    void resetAngles();
    void teleportTo(const glm::vec3& pos);
    void setOnlineMode(bool online) { onlineMode = online; }

    // Last known safe position (grounded, not falling)
    bool hasLastSafePosition() const { return hasLastSafe_; }
    const glm::vec3& getLastSafePosition() const { return lastSafePos_; }
    float getContinuousFallTime() const { return continuousFallTime_; }

    // Auto-unstuck callback (triggered when falling too long)
    using AutoUnstuckCallback = std::function<void()>;
    void setAutoUnstuckCallback(AutoUnstuckCallback cb) { autoUnstuckCallback_ = std::move(cb); }
    void startIntroPan(float durationSec = 2.8f, float orbitDegrees = 140.0f);
    bool isIntroActive() const { return introActive; }
    bool isIdleOrbit() const { return idleOrbit_; }
    void setIdleOrbitEnabled(bool enabled) {
        idleOrbitEnabled_ = enabled;
        if (!enabled && idleOrbit_) {
            introActive = false;
            idleOrbit_ = false;
            idleTimer_ = 0.0f;
        }
    }
    bool isIdleOrbitEnabled() const { return idleOrbitEnabled_; }

    float getMovementSpeed() const { return movementSpeed; }
    const glm::vec3& getDefaultPosition() const { return defaultPosition; }
    bool isMoving() const;
    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }
    float getFacingYaw() const { return facingYaw; }
    float getTravelYaw() const { return travelYaw_; }
    bool isThirdPerson() const { return thirdPerson; }
    bool isFirstPersonView() const { return thirdPerson && (userTargetDistance <= MIN_DISTANCE + 0.15f); }
    bool isGrounded() const { return grounded; }
    bool isJumping() const { return !grounded && verticalVelocity > 0.0f; }
    // A swimming character is not grounded and is not rising, but it is not
    // falling either — treating it as falling made leaving the water register as
    // a hard landing.
    bool isFalling() const { return !grounded && !swimming && verticalVelocity <= 0.0f; }

    // Call every frame while riding a transport that forces Z to a locked value
    // externally (e.g. the Deeprun Tram, which has no real floor mid-tunnel).
    // Without this, gravity keeps integrating verticalVelocity every frame since
    // grounded never goes true on a moving M2 deck, even though the position
    // itself is being overwritten and looks static - the huge fall speed that
    // silently built up over the whole ride would then unleash all at once the
    // instant the external Z lock releases at disembark, clipping the player
    // through the floor.
    void suppressVerticalPhysics() { verticalVelocity = 0.0f; grounded = true; }
    bool isJumpKeyPressed() const { return jumpBufferTimer > 0.0f; }
    bool isSprinting() const;
    bool isMovingForward() const { return moveForwardActive; }
    bool isMovingBackward() const { return moveBackwardActive; }
    bool isStrafingLeft() const { return strafeLeftActive; }
    bool isStrafingRight() const { return strafeRightActive; }
    bool isTurningLeft() const { return turningLeftActive; }
    bool isTurningRight() const { return turningRightActive; }
    bool isAutoRunning() const { return autoRunning; }
    bool isRightMouseHeld() const { return rightMouseDown; }
    bool isSitting() const { return sitting; }
    bool isSwimming() const { return swimming; }
    bool isInsideWMO() const { return cachedInsideWMO; }
    bool isInsideInteriorWMO() const { return cachedInsideInteriorWMO; }
    void setGrounded(bool g) { grounded = g; }
    void setSitting(bool s) { sitting = s; }
    bool isOnTaxi() const { return externalFollow_; }
    const glm::vec3* getFollowTarget() const { return followTarget; }
    glm::vec3* getFollowTargetMutable() { return followTarget; }

    // Movement callback for sending opcodes to server
    using MovementCallback = std::function<void(uint32_t opcode)>;
    void setMovementCallback(MovementCallback cb) { movementCallback = std::move(cb); }

    // Callback invoked when the player stands up via local input (space/X/movement key
    // while server-sitting), so the caller can send CMSG_STAND_STATE_CHANGE(0).
    using StandUpCallback = std::function<void()>;
    void setStandUpCallback(StandUpCallback cb) { standUpCallback_ = std::move(cb); }

    // Callback invoked when the player sits down via local input (X key).
    using SitDownCallback = std::function<void()>;
    void setSitDownCallback(SitDownCallback cb) { sitDownCallback_ = std::move(cb); }

    // Callback invoked when auto-follow is cancelled by user movement input.
    using AutoFollowCancelCallback = std::function<void()>;
    void setAutoFollowCancelCallback(AutoFollowCancelCallback cb) { autoFollowCancelCallback_ = std::move(cb); }

    void setUseWoWSpeed(bool use) { useWoWSpeed = use; }
    void setRunSpeedOverride(float speed) { runSpeedOverride_ = speed; }
    void setWalkSpeedOverride(float speed) { walkSpeedOverride_ = speed; }
    void setSwimSpeedOverride(float speed) { swimSpeedOverride_ = speed; }
    void setSwimBackSpeedOverride(float speed) { swimBackSpeedOverride_ = speed; }
    void setFlightSpeedOverride(float speed) { flightSpeedOverride_ = speed; }
    void setFlightBackSpeedOverride(float speed) { flightBackSpeedOverride_ = speed; }
    void setRunBackSpeedOverride(float speed) { runBackSpeedOverride_ = speed; }
    // Server turn rate in rad/s (SMSG_FORCE_TURN_RATE_CHANGE); 0 = use WOW_TURN_SPEED default
    void setTurnRateOverride(float rateRadS) { turnRateOverride_ = rateRadS; }
    void setMovementRooted(bool rooted) { movementRooted_ = rooted; }
    bool isMovementRooted() const { return movementRooted_; }
    void setGravityDisabled(bool disabled) { gravityDisabled_ = disabled; }
    void setFeatherFallActive(bool active) { featherFallActive_ = active; }
    void setWaterWalkActive(bool active) { waterWalkActive_ = active; }
    void setFlyingActive(bool active) { flyingActive_ = active; }
    bool isFlyingActive() const { return flyingActive_; }
    bool isAscending() const { return wasAscending_; }
    bool isDescending() const { return wasDescending_; }
    void setHoverActive(bool active) { hoverActive_ = active; }
    void setMounted(bool m) { mounted_ = m; }
    void setIntoxication(float amount) { intoxication_ = std::clamp(amount, 0.0f, 1.0f); }
    void setMountHeightOffset(float offset) { mountHeightOffset_ = offset; }
    void setExternalFollow(bool enabled) { externalFollow_ = enabled; }
    void setExternalMoving(bool moving) { externalMoving_ = moving; }
    void setFacingYaw(float yaw) { facingYaw = yaw; }  // For taxi/scripted movement
    void clearMovementInputs();
    void suppressMovementFor(float seconds) { movementSuppressTimer_ = seconds; }
    void suspendGravityFor(float seconds) { gravitySuspendTimer_ = seconds; }

    // Auto-follow: walk toward a target position each frame (WoW /follow).
    // The caller updates *targetPos every frame with the followed entity's render position.
    // Stops within FOLLOW_STOP_DIST; cancels on manual WASD input.
    void setAutoFollow(const glm::vec3* targetPos) { autoFollowTarget_ = targetPos; }
    void cancelAutoFollow() { autoFollowTarget_ = nullptr; }
    bool isAutoFollowing() const { return autoFollowTarget_ != nullptr; }

    // Trigger mount jump (applies vertical velocity for physics hop)
    void triggerMountJump();

    // Apply server-driven knockback impulse.
    // dir: render-space 2D direction unit vector (from vcos/vsin in packet)
    // hspeed: horizontal speed magnitude (units/s)
    // vspeed: raw packet vspeed field (server sends negative for upward launch)
    void applyKnockBack(float vcos, float vsin, float hspeed, float vspeed);

    // Trigger a camera shake effect (e.g. from SMSG_CAMERA_SHAKE).
    // magnitude: peak positional offset in world units
    // frequency: oscillation frequency in Hz
    // duration: shake duration in seconds
    void triggerShake(float magnitude, float frequency, float duration);

    // For first-person player hiding
    void setCharacterRenderer(class CharacterRenderer* cr, uint32_t playerId) {
        characterRenderer = cr;
        playerInstanceId = playerId;
    }

private:
    Camera* camera;
    TerrainManager* terrainManager = nullptr;
    WMORenderer* wmoRenderer = nullptr;
    M2Renderer* m2Renderer = nullptr;
    WaterRenderer* waterRenderer = nullptr;
    CharacterRenderer* characterRenderer = nullptr;
    uint32_t playerInstanceId = 0;

    // Stored rotation (avoids lossy forward-vector round-trip)
    float yaw = 180.0f;
    float pitch = -30.0f;
    float facingYaw = 180.0f;  // Character-facing yaw (can differ from camera yaw)
    float travelYaw_ = 180.0f; // Heading of actual movement vector (see getTravelYaw)

    // Movement settings
    float movementSpeed = 50.0f;
    float sprintMultiplier = 3.0f;
    float slowMultiplier = 0.3f;

    // Mouse settings
    float mouseSensitivity = 0.2f;
    bool invertMouse = false;
    bool relativeMouseEnabled_ = true;
    bool mouseButtonDown = false;
    bool leftMouseDown = false;
    bool rightMouseDown = false;
    // Camera rotation only begins once the mouse moves past a small dead-zone while a
    // button is held. This keeps a click (to select an NPC) from nudging the view on
    // tiny hand jitter — which made NPCs appear to shift out from under the cursor.
    bool rotateArmed_ = false;
    float dragPixelsSincePress_ = 0.0f;
    static constexpr float kRotateDeadzonePixels = 5.0f;

    // Third-person orbit camera (WoW-style)
    bool thirdPerson = false;
    float userTargetDistance = 10.0f;   // What the player wants (scroll wheel)
    float currentDistance = 10.0f;      // Smoothed actual distance
    float collisionDistance = 10.0f;    // Max allowed by collision
    bool externalFollow_ = false;
    static constexpr float MIN_DISTANCE = 0.5f;     // Minimum zoom (first-person threshold)
    static constexpr float MAX_DISTANCE_NORMAL = 22.0f;   // Default max zoom out
    static constexpr float MAX_DISTANCE_EXTENDED = 50.0f;  // Extended max zoom out
    static constexpr float MAX_DISTANCE_INTERIOR = 12.0f;  // Max zoom inside WMOs
    bool extendedZoom_ = false;
    static constexpr float ZOOM_SMOOTH_SPEED = 15.0f;  // How fast zoom eases
    static constexpr float CAM_SMOOTH_SPEED_DEFAULT = 30.0f;
    float camSmoothSpeed_ = CAM_SMOOTH_SPEED_DEFAULT;  // User-configurable camera smoothing (higher = tighter)
    // When true the camera keeps its exponential lerp even while actively
    // dragging or keyboard-turning — the pre-snap "floaty follow" feel.
    // When false (default), rotation input moves the camera 1:1.
    bool smoothCameraFollow_ = false;
public:
    void setCameraSmoothSpeed(float speed) { camSmoothSpeed_ = std::clamp(speed, 5.0f, 100.0f); }
    float getCameraSmoothSpeed() const { return camSmoothSpeed_; }
    void setSmoothCameraFollow(bool smooth) { smoothCameraFollow_ = smooth; }
    bool isSmoothCameraFollow() const { return smoothCameraFollow_; }
    void setPivotHeight(float h) { pivotHeight_ = std::clamp(h, 0.0f, 3.0f); }
    float getPivotHeight() const { return pivotHeight_; }
private:
    static constexpr float PIVOT_HEIGHT_DEFAULT = 1.6f;
    float pivotHeight_ = PIVOT_HEIGHT_DEFAULT;  // User-configurable pivot height
    static constexpr float CAM_SPHERE_RADIUS = 0.32f;  // Keep camera farther from geometry to avoid clipping-through surfaces
    static constexpr float CAM_EPSILON = 0.22f;        // Extra wall offset to avoid near-plane clipping artifacts
    static constexpr float COLLISION_FOCUS_RADIUS_THIRD_PERSON = 20.0f;  // Reduced for performance
    static constexpr float COLLISION_FOCUS_RADIUS_FREE_FLY = 20.0f;
    static constexpr float MIN_PITCH = -88.0f;      // Look almost straight down
    static constexpr float MAX_PITCH = 88.0f;       // Look almost straight up (WoW standard)
    glm::vec3* followTarget = nullptr;
    glm::vec3 smoothedCamPos = glm::vec3(0.0f);     // For smooth camera movement
    float smoothedCollisionDist_ = -1.0f;           // Asymmetrically-smoothed WMO collision limit (-1 = uninitialised)

    // Gravity / grounding
    float verticalVelocity = 0.0f;
    bool grounded = false;
    static constexpr float STAND_EYE_HEIGHT = 1.2f;  // Standing eye height
    static constexpr float CROUCH_EYE_HEIGHT = 0.6f; // Crouching eye height
    float eyeHeight = STAND_EYE_HEIGHT;
    float lastGroundZ = 0.0f;  // Last known ground height (fallback when no terrain)
    glm::vec3 lastGroundedPos_{0.0f};  // Last position that had ground under it (void recovery)
    bool hasLastGroundedPos_ = false;
    static constexpr float GRAVITY = -30.0f;
    static constexpr float JUMP_VELOCITY = 15.0f;
    float jumpBufferTimer = 0.0f;   // Time since space was pressed
    float coyoteTimer = 0.0f;       // Time since last grounded
    static constexpr float JUMP_BUFFER_TIME = 0.15f;  // 150ms input buffer
    static constexpr float COYOTE_TIME = 0.10f;        // 100ms grace after leaving ground

    // Cached isInsideWMO result (throttled to avoid per-frame cost)
    bool cachedInsideWMO = false;
    bool cachedInsideInteriorWMO = false;
    int insideStateCheckCounter_ = 0;
    glm::vec3 lastInsideStateCheckPos_ = glm::vec3(0.0f);
    int insideWMOCheckCounter = 0;
    glm::vec3 lastInsideWMOCheckPos = glm::vec3(0.0f);

    // Cached camera WMO floor query (skip if camera moved < 0.3 units)
    glm::vec3 lastCamFloorQueryPos = glm::vec3(0.0f);
    std::optional<float> cachedCamWmoFloor;
    bool hasCachedCamFloor = false;

    // Terrain-aware camera pivot lift cache (throttled for performance).
    glm::vec3 lastPivotLiftQueryPos_ = glm::vec3(0.0f);
    float lastPivotLiftDistance_ = 0.0f;
    int pivotLiftQueryCounter_ = 0;
    float cachedPivotLift_ = 0.0f;
    static constexpr int PIVOT_LIFT_QUERY_INTERVAL = 3;
    static constexpr float PIVOT_LIFT_POS_THRESHOLD = 0.5f;
    static constexpr float PIVOT_LIFT_DIST_THRESHOLD = 0.5f;

    // Cached floor height queries (update every 5 frames or 2 unit movement)
    glm::vec3 lastFloorQueryPos = glm::vec3(0.0f);
    std::optional<float> cachedFloorHeight;
    int floorQueryFrameCounter = 0;
    static constexpr float FLOOR_QUERY_DISTANCE_THRESHOLD = 2.0f;  // Increased from 1.0
    static constexpr int FLOOR_QUERY_FRAME_INTERVAL = 5;  // Increased from 3

    // Helper to get cached floor height (reduces expensive queries)
    std::optional<float> getCachedFloorHeight(float x, float y, float z);

    // Ray-march the terrain heightfield along pivot→camDir and return the
    // farthest camera distance that keeps clearance above the terrain surface.
    // Returns maxDist when the ray stays clear (or terrain is unavailable).
    float raymarchTerrainCameraLimit(const glm::vec3& pivot, const glm::vec3& camDir,
                                     float maxDist) const;

    // Swimming
    bool swimming = false;
    bool wasSwimming = false;
    static constexpr float SWIM_SPEED_FACTOR = 0.67f;
    static constexpr float SWIM_GRAVITY = -5.0f;
    static constexpr float SWIM_BUOYANCY = 8.0f;
    static constexpr float SWIM_SINK_SPEED = -3.0f;
    // How fast vertical motion bleeds off when no swim-up or swim-down key is
    // held. Retail holds depth rather than drifting, so this only arrests the
    // momentum carried in from a jump or a dive.
    static constexpr float SWIM_VERTICAL_DAMPING = 6.0f;
    // Within this distance of the surface a body floats up to it; deeper than
    // this it holds depth. Retail lets a character idle underwater — and drown
    // doing it — so buoyancy cannot reach all the way down.
    static constexpr float SWIM_FLOAT_BAND = 1.3f;
    static constexpr float WATER_SURFACE_OFFSET = 0.9f;

    // Movement input suppression (after teleport/portal, ignore held keys)
    float movementSuppressTimer_ = 0.0f;
    // Gravity suspension (after world entry, hold Z until ground detected)
    float gravitySuspendTimer_ = 0.0f;

    // State
    bool enabled = true;
    bool sitting = false;
    bool xKeyWasDown = false;
    bool rKeyWasDown = false;
    bool runPace = false;
    bool autoRunning = false;
    bool tildeWasDown = false;

    // Auto-follow target position (WoW /follow). Non-null when following.
    const glm::vec3* autoFollowTarget_ = nullptr;
    static constexpr float FOLLOW_STOP_DIST = 3.0f;   // Stop within 3 units of target
    static constexpr float FOLLOW_MAX_DIST  = 40.0f;  // Cancel if > 40 units away

    // Movement state tracking (for sending opcodes on state change)
    bool wasMovingForward = false;
    bool wasMovingBackward = false;
    bool wasStrafingLeft = false;
    bool wasStrafingRight = false;
    bool wasTurningLeft = false;
    bool wasTurningRight = false;
    bool wasJumping = false;
    bool wasFalling = false;
    bool wasAscending_ = false;   // Space held while flyingActive_
    bool wasDescending_ = false;  // X held while flyingActive_
    bool moveForwardActive = false;
    bool moveBackwardActive = false;
    bool strafeLeftActive = false;
    bool strafeRightActive = false;
    bool turningLeftActive = false;
    bool turningRightActive = false;

    // Movement callback
    MovementCallback movementCallback;
    StandUpCallback standUpCallback_;
    SitDownCallback sitDownCallback_;
    AutoFollowCancelCallback autoFollowCancelCallback_;

    // Movement speeds
    bool useWoWSpeed = false;
    static constexpr float WOW_RUN_SPEED = 7.0f;     // Normal run (WotLK)
    static constexpr float WOW_SPRINT_SPEED = 10.5f; // Optional fast mode (not default WoW behavior)
    static constexpr float WOW_WALK_SPEED = 2.5f;    // Walk
    static constexpr float WOW_BACK_SPEED = 4.5f;    // Backpedal
    static constexpr float WOW_TURN_SPEED = 180.0f;  // Keyboard turn deg/sec
    static constexpr float WOW_GRAVITY = -19.29f;
    static constexpr float WOW_JUMP_VELOCITY = 7.96f;
    static constexpr float MOUNT_GRAVITY = -18.0f;       // Snappy WoW-feel jump
    static constexpr float MOUNT_JUMP_HEIGHT = 1.0f;    // Desired jump height in meters

    // Computed jump velocity using vz = sqrt(2 * g * h)
    static inline float getMountJumpVelocity() {
        return std::sqrt(2.0f * std::abs(MOUNT_GRAVITY) * MOUNT_JUMP_HEIGHT);
    }

    // Server-driven speed overrides (0 = use hardcoded default)
    float runSpeedOverride_ = 0.0f;
    float walkSpeedOverride_ = 0.0f;
    float swimSpeedOverride_ = 0.0f;
    float swimBackSpeedOverride_ = 0.0f;
    float flightSpeedOverride_ = 0.0f;
    float flightBackSpeedOverride_ = 0.0f;
    float runBackSpeedOverride_ = 0.0f;
    float turnRateOverride_ = 0.0f;  // rad/s; 0 = WOW_TURN_SPEED default (π rad/s)
    // Server-driven root state: when true, block all horizontal movement input.
    bool movementRooted_ = false;
    // Server-driven gravity disable (levitate/hover): skip gravity accumulation.
    bool gravityDisabled_ = false;
    // Server-driven feather fall: cap downward velocity to slow-fall terminal.
    bool featherFallActive_ = false;
    // Server-driven water walk: treat water surface as ground (don't swim).
    bool waterWalkActive_ = false;
    // Player-controlled flight (CAN_FLY + FLYING): 3D movement, no gravity.
    bool flyingActive_ = false;
    // Server-driven hover (HOVER flag): float at fixed height above ground.
    bool hoverActive_ = false;
    bool mounted_ = false;
    float mountHeightOffset_ = 0.0f;
    bool externalMoving_ = false;

    // Online mode: trust server position, don't prefer outdoors over WMO floors
    bool onlineMode = false;

    // Default spawn position (Goldshire Inn)
    glm::vec3 defaultPosition = glm::vec3(-9464.0f, 62.0f, 200.0f);
    float defaultYaw = 0.0f;
    float defaultPitch = -5.0f;

    // Spawn intro camera pan
    bool introActive = false;
    float introTimer = 0.0f;
    float introDuration = 0.0f;
    float introStartYaw = 0.0f;
    float introEndYaw = 0.0f;
    float introOrbitDegrees = 0.0f;
    float introStartPitch = -15.0f;
    float introEndPitch = -5.0f;
    float introStartDistance = 12.0f;
    float introEndDistance = 10.0f;

    // Idle camera: triggers intro pan after IDLE_TIMEOUT seconds of no input
    float idleTimer_ = 0.0f;
    bool idleOrbit_ = false;  // true when current intro pan is an idle orbit (loops)
    bool idleOrbitEnabled_ = true;
    static constexpr float IDLE_TIMEOUT = 120.0f; // 2 minutes

    // Last known safe position (saved periodically when grounded on real geometry)
    bool hasLastSafe_ = false;
    glm::vec3 lastSafePos_ = glm::vec3(0.0f);
    float safePosSaveTimer_ = 0.0f;
    bool hasRealGround_ = false; // True only when terrain/WMO/M2 floor is detected
    static constexpr float SAFE_POS_SAVE_INTERVAL = 2.0f; // Save every 2 seconds

    // No-ground timer: after grace period, let the player fall instead of hovering
    float noGroundTimer_ = 0.0f;
    // Edge trigger for the terrain-penetration rescue's log line. It fires every
    // frame of a steep climb, and the interesting event is that it started —
    // above all if it ever starts somewhere it should not, like a crypt.
    bool terrainRescueActive_ = false;
    static constexpr float NO_GROUND_GRACE = 0.5f; // 500ms grace for terrain streaming

    // Continuous fall time (for auto-unstuck detection)
    float continuousFallTime_ = 0.0f;
    bool autoUnstuckFired_ = false;
    AutoUnstuckCallback autoUnstuckCallback_;
    static constexpr float AUTO_UNSTUCK_FALL_TIME = 5.0f; // 5 seconds of falling

    // Collision query cache (skip expensive checks if position barely changed)
    glm::vec3 lastCollisionCheckPos_ = glm::vec3(0.0f);
    float cachedFloorHeight_ = 0.0f;
    bool hasCachedFloor_ = false;
    static constexpr float COLLISION_CACHE_DISTANCE = 0.15f;  // Re-check every 15cm

    // Server-driven knockback state.
    // When the server sends SMSG_MOVE_KNOCK_BACK, we apply horizontal + vertical
    // impulse here and let the normal physics loop (gravity, collision) resolve it.
    bool knockbackActive_ = false;
    glm::vec2 knockbackHorizVel_ = glm::vec2(0.0f); // render-space horizontal velocity (units/s)
    // Horizontal velocity decays via WoW-like drag so the player doesn't slide forever.
    static constexpr float KNOCKBACK_HORIZ_DRAG = 4.5f; // exponential decay rate (1/s)

    // Camera shake state (SMSG_CAMERA_SHAKE)
    float shakeElapsed_   = 0.0f;
    float shakeDuration_  = 0.0f;
    float shakeMagnitude_ = 0.0f;
    float shakeFrequency_ = 0.0f;

    // Server-authored drunkenness (0 sober, 1 smashed).
    float intoxication_ = 0.0f;
    float intoxicationTime_ = 0.0f;
};

} // namespace rendering
} // namespace wowee
