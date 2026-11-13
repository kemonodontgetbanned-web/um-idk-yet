#pragma once

#include <cstdint>

#include <string>

namespace Structs {
    inline std::string ClientVersion = "2.741.1061";

    using BYTE = unsigned char;
    struct Vector2 { float x, y; };
    struct Vector3 { float x, y, z; };
    struct Vector4 { float x, y, z, w; };
    struct Matrix3x3 { float m[9]; };
    struct Matrix4x4 { float m[16]; };
    struct Color3 { float r, g, b; };
    struct CFrame { Matrix3x3 rot; Vector3 pos; };

    struct TaskScheduler {
        char pad_0[0x18];
        std::string JobName;
        char pad_1[0x98];
        double MaxFPS;
        char pad_2[0x10];
        uintptr_t JobStart;
        uintptr_t JobEnd;
        char pad_3[0x79652f8];
        uintptr_t Pointer;
    };

    struct RenderJob {
        char pad_4[0x30];
        uintptr_t FakeDataModel;
        char pad_5[0x1b8];
        uintptr_t RealDataModel;
    };

    struct FakeDataModel {
        char pad_6[0x1f0];
        uintptr_t RealDataModel;
    };

    struct VisualEngine {
        char pad_7[0x120];
        Matrix4x4 ViewMatrix;
        char pad_8[0x9a0];
        Vector2 Dimensions;
        char pad_9[0x791cee8];
        uintptr_t Pointer;
    };

    struct Instance {
        char pad_10[0x8];
        std::string Name;
        uintptr_t ChildrenEnd;
        std::string ClassName;
        char pad_11[0x8];
        uintptr_t ClassDescriptor;
        char pad_12[0x48];
        uintptr_t Parent;
        uintptr_t NameContainer;
        uintptr_t ChildrenStart;
        char pad_13[0x130];
        uintptr_t ClassBase;
    };

    struct Misc {
        char pad_14[0xa9];
        std::string Value;
        char pad_15[0x2f];
        uintptr_t Adornee;
    };

    struct DataModel {
        char pad_16[0x110];
        std::string JobId;
        char pad_17[0x28];
        uintptr_t Workspace;
        char pad_18[0x20];
        uintptr_t CreatorId;
        uintptr_t GameId;
        uintptr_t PlaceId;
        char pad_19[0x14];
        int PlaceVersion;
        char pad_20[0x230];
        int PrimitiveCount;
        char pad_21[0x74];
        uintptr_t ScriptContext;
        char pad_22[0x101];
        std::string ServerIP;
    };

    struct RunService {
        char pad_23[0xb8];
        double HeartbeatFPS;
        char pad_24[0x10];
        uintptr_t HeartbeatTask;
    };

    struct Player {
        char pad_25[0xc0];
        uintptr_t UserId;
        char pad_26[0x31];
        std::string LocaleId;
        char pad_27[0xf];
        uintptr_t LocalPlayer;
        std::string DisplayName;
        char pad_28[0x127];
        uintptr_t ModelInstance;
        char pad_29[0x38];
        uintptr_t Team;
        char pad_30[0x7c];
        int AccountAge;
        char pad_31[0x8];
        float MaxZoomDistance;
        float MinZoomDistance;
        int CameraMode;
        char pad_32[0x20];
        float HealthDisplayDistance;
        char pad_33[0xc];
        float NameDisplayDistance;
        char pad_34[0x8];
        int TeamColor;
    };

    struct Humanoid {
        char pad_35[0xa9];
        std::string DisplayName;
        char pad_36[0x37];
        uintptr_t SeatPart;
        char pad_37[0x8];
        uintptr_t MoveToPart;
        char pad_38[0x8];
        Vector3 CameraOffset;
        char pad_39[0xc];
        Vector3 MoveDirection;
        Vector3 TargetPoint;
        char pad_40[0xc];
        Vector3 MoveToPoint;
        char pad_41[0x4];
        uintptr_t WalkToPoint;
        char pad_42[0x4];
        bool Jump;
        int DisplayDistanceType;
        char pad_43[0x4];
        float HealthDisplayDistance;
        int HealthDisplayType;
        float Health;
        float HipHeight;
        char pad_44[0x8];
        float JumpHeight;
        float JumpPower;
        float MaxHealth;
        float MaxSlopeAngle;
        float NameDisplayDistance;
        int NameOcclusion;
        char pad_45[0x8];
        int RigType;
        char pad_46[0xc];
        float Walkspeed;
        bool AutoJumpEnabled;
        bool AutoRotate;
        bool AutomaticScalingEnabled;
        bool BreakJointsOnDeath;
        bool EvaluateStateMachine;
        char pad_47[0x3];
        bool PlatformStand;
        bool RequiresNeck;
        bool Sit;
        char pad_48[0x1];
        bool UseJumpPower;
        char pad_49[0x1cb];
        float WalkspeedCheck;
        char pad_50[0x40];
        double WalkTimer;
        char pad_51[0x60];
        uintptr_t HumanoidRootPart;
        char pad_52[0xb24];
        int FloorMaterial;
    };

    struct Team {
        char pad_53[0xa8];
        int BrickColor;
        unsigned int TeamColor;
    };

    struct Seat {
        char pad_54[0x1f8];
        uintptr_t Occupant;
    };

    struct VehicleSeat {
        char pad_55[0x208];
        float MaxSpeed;
        float SteerFloat;
        float ThrottleFloat;
        float Torque;
        float TurnSpeed;
    };

    struct Tool {
        char pad_56[0x3f1];
        std::string Tooltip;
        char pad_57[0x33];
        Vector3 Grip;
        char pad_58[0x4d5];
        bool Enabled;
        char pad_59[0x2];
        bool ManualActivationOnly;
        bool RequiresHandle;
        char pad_60[0x2];
        bool CanBeDropped;
    };

    struct CharacterMesh {
        char pad_61[0xb9];
        std::string BaseTextureId;
        char pad_62[0x20];
        std::string MeshId;
        char pad_63[0x20];
        std::string OverlayTextureId;
        char pad_64[0xf];
        int BodyPart;
    };

    struct Workspace {
        char pad_65[0x3c0];
        uintptr_t World;
        char pad_66[0x98];
        uintptr_t CurrentCamera;
        char pad_67[0x18];
        double DistributedGameTime;
        char pad_68[0x4b8];
        float ReadOnlyGravity;
    };

    struct Camera {
        char pad_69[0xb0];
        uintptr_t CameraSubject;
        char pad_70[0x8];
        Matrix3x3 Rotation;
        Vector3 Position;
        char pad_71[0x30];
        int CameraType;
        char pad_72[0x4];
        float FieldOfView;
        char pad_73[0x148];
        short Viewport;
        char pad_74[0x3e];
        Vector2 ViewportSize;
        float ImagePlaneDepth;
    };

    struct BasePart {
        char pad_75[0xf4];
        float Reflectance;
        char pad_76[0x25];
        bool CastShadow;
        bool Locked;
        bool Massless;
        char pad_77[0x48];
        uintptr_t Primitive;
        char pad_78[0x28];
        int Shape;
    };

    struct Primitive {
        char pad_79[0xb0];
        CFrame CFrame_;
        Matrix3x3 Rotation;
        Vector3 Position;
        Vector3 AssemblyLinearVelocity;
        Vector3 AssemblyAngularVelocity;
        char pad_80[0xbe];
        BYTE Flags;
        char pad_81[0x5];
        Vector3 Size;
        char pad_82[0x48];
        uintptr_t Owner;
    };

    struct Model {
        char pad_83[0x12c];
        float Scale;
        char pad_84[0xd8];
        uintptr_t PrimaryPart;
    };

    struct WorldRoot {
        char pad_85[0x80];
        uintptr_t RaycastBoundFn;
        char pad_86[0x76322f8];
        uintptr_t RaycastBoundDesc;
    };

    struct MouseService {
        char pad_87[0xe0];
        uintptr_t InputObject;
        char pad_88[0x8];
        uintptr_t InputObject2;
    };

    struct InputObject {
        char pad_89[0xc4];
        Vector2 MousePosition;
    };

    struct World {
        char pad_90[0x214];
        float FallenPartsDestroyHeight;
        char pad_91[0x8];
        float Gravity;
        char pad_92[0x14];
        uintptr_t AirProperties;
        char pad_93[0x68];
        uintptr_t Primitives;
        char pad_94[0x430];
        float worldStepsPerSec;
    };

    struct AirProperties {
        char pad_95[0x10];
        float AirDensity;
        char pad_96[0x20];
        Vector3 GlobalWind;
    };

    struct Terrain {
        char pad_97[0x1c0];
        Vector3 WaterColor;
        char pad_98[0x4];
        float GrassLength;
        char pad_99[0x4];
        float WaterReflectance;
        float WaterTransparency;
        float WaterWaveSize;
        float WaterWaveSpeed;
        char pad_100[0x2b0];
        uintptr_t MaterialColors;
    };

    struct MaterialColors {
        char pad_101[0x6];
        uintptr_t Grass;
        uintptr_t Slate;
        uintptr_t Concrete;
        uintptr_t Brick;
        uintptr_t Sand;
        uintptr_t WoodPlanks;
        uintptr_t Rock;
        uintptr_t Glacier;
        uintptr_t Snow;
        uintptr_t Sandstone;
        uintptr_t Mud;
        uintptr_t Basalt;
        uintptr_t Ground;
        uintptr_t CrackedLava;
        uintptr_t Asphalt;
        uintptr_t Cobblestone;
        uintptr_t Ice;
        uintptr_t LeafyGrass;
        uintptr_t Salt;
        uintptr_t Limestone;
        uintptr_t Pavement;
    };

    struct Sound {
        char pad_102[0xb9];
        std::string SoundId;
        char pad_103[0xf];
        uintptr_t SoundGroup;
        char pad_104[0x2c];
        float PlaybackSpeed;
        float RollOffMaxDistance;
        float RollOffMinDistance;
        char pad_105[0x8];
        float Volume;
        char pad_106[0x9];
        bool Looped;
        char pad_107[0x2];
        bool IsPlaying;
    };

    struct SpawnLocation {
        char pad_108[0x1c8];
        int ForcefieldDuration;
        int TeamColor;
        bool AllowTeamChangeOnTouch;
        bool Enabled;
        bool Neutral;
    };

    struct SurfaceAppearance {
        char pad_109[0x1b0];
        int AlphaMode;
        float EmissiveStrength;
    };

    struct ParticleEmitter {
        char pad_110[0x1c8];
        Vector3 Acceleration;
        char pad_111[0x8];
        Vector2 Lifetime;
        float RotSpeed;
        char pad_112[0x4];
        float Rotation;
        char pad_113[0x4];
        float Speed;
        char pad_114[0x4];
        Vector2 SpreadAngle;
        float Brightness;
        float Drag;
        char pad_115[0x14];
        float LightEmission;
        float LightInfluence;
        char pad_116[0x8];
        float Rate;
        char pad_117[0x10];
        float TimeScale;
        float VelocityInheritance;
        float ZOffset;
    };

    struct Beam {
        char pad_118[0x148];
        uintptr_t Attachment0;
        char pad_119[0x8];
        uintptr_t Attachment1;
        char pad_120[0x8];
        float Brightness;
        float CurveSize0;
        float CurveSize1;
        float LightEmission;
        float LightInfluence;
        char pad_121[0x8];
        float TextureLength;
        char pad_122[0x4];
        float TextureSpeed;
        float Width0;
        float Width1;
        float ZOffset;
    };

    struct Lighting {
        char pad_123[0xb8];
        float ClockTime;
        char pad_124[0x4];
        Vector3 Ambient;
        Vector3 ColorShift_Top;
        Vector3 ColorShift_Bottom;
        Vector3 FogColor;
        Vector3 OutdoorAmbient;
        char pad_125[0xc];
        float Brightness;
        float EnvironmentDiffuseScale;
        float EnvironmentSpecularScale;
        float ExposureCompensation;
        char pad_126[0x4];
        float FogEnd;
        float FogStart;
        float GeographicLatitude;
        char pad_127[0x18];
        Color3 GradientTop;
        Color3 LightColor;
        Vector3 SunPosition;
        char pad_128[0x10];
        Vector3 MoonPosition;
        Color3 GradientBottom;
        char pad_129[0x2c];
        uintptr_t Sky;
    };

    struct Sky {
        char pad_130[0x4];
        float SunAngularSize;
        float MoonAngularSize;
        char pad_131[0x1f0];
        int StarCount;
        char pad_132[0x4e0];
        std::string SunTextureId;
        char pad_133[0x4];
        Vector3 SkyboxOrientation;
    };

    struct Atmosphere {
        char pad_134[0xa8];
        Vector3 Color;
        char pad_135[0xc];
        float Density;
        float Glare;
        float Haze;
        float Offset;
    };

    struct BloomEffect {
        char pad_136[0xa0];
        float Intensity;
        float Size;
        float Threshold;
        char pad_137[0x54];
        bool Enabled;
    };

    struct DepthOfFieldEffect {
        char pad_138[0xa0];
        float FarIntensity;
        float FocusDistance;
        float InFocusRadius;
        float NearIntensity;
    };

    struct SunRaysEffect {
        char pad_139[0xa0];
        float Intensity;
        float Spread;
    };

    struct ColorCorrectionEffect {
        char pad_140[0xac];
        float Brightness;
        float Contrast;
    };

    struct ColorGradingEffect {
        char pad_141[0xa0];
        int TonemapperPreset;
    };

    struct BlurEffect {
        char pad_142[0xa0];
        float Size;
    };

    struct ProximityPrompt {
        char pad_143[0xa1];
        std::string ActionText;
        char pad_144[0x10];
        std::string ObjectText;
        char pad_145[0x3b];
        int GamepadKeyCode;
        float HoldDuration;
        int KeyCode;
        float MaxActivationDistance;
        char pad_146[0xa];
        bool Enabled;
        bool RequiresLineOfSight;
    };

    struct ClickDetector {
        char pad_147[0xd0];
        float MaxActivationDistance;
    };

    struct DragDetector {
        char pad_148[0xd0];
        float MaxActivationDistance;
        char pad_149[0xf4];
        uintptr_t ReferenceInstance;
        char pad_150[0x74];
        Vector3 MaxDragTranslation;
        Vector3 MinDragTranslation;
        char pad_151[0x24];
        float MaxDragAngle;
        float MaxForce;
        float MaxTorque;
        float MinDragAngle;
        char pad_152[0x8];
        float Responsiveness;
    };

    struct StatsItem {
        char pad_153[0x180];
        uintptr_t Value;
    };
}
