// Copyright 2025-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <moonray/rendering/pbr/core/RayState.h>

#include <scene_rdl2/common/fb_util/FbTypes.h>
#include <scene_rdl2/common/grid_util/Arg.h>
#include <scene_rdl2/common/grid_util/Parser.h>
#include <scene_rdl2/common/grid_util/VectorPacket.h>

namespace moonray {

namespace mcrt_common { struct Frustum; }

namespace pbr {

class Camera;

/// --------------------------------------------------------------------------------------------------------------------

// All of the user parameters used to filter the nodes
class PathVisualizerParams {
public:
    using Arg = scene_rdl2::grid_util::Arg;
    using Parser = scene_rdl2::grid_util::Parser; 

    PathVisualizerParams() { parserConfigure(); }

    void setOn(const bool flag) { mOn = flag; }

    uint32_t mPixelX                = 0;       // x-coord for user-chosen pixel
    uint32_t mPixelY                = 0;       // y-coord for user-chosen pixel
    uint32_t mMaxDepth              = 1;       // max number of bounces
    uint32_t mPixelSamples          = 4;       // # of pixel samples
    uint32_t mLightSamples          = 1;       // # of light samples
    uint32_t mBsdfSamples           = 1;       // # of bsdf samples
    bool mUseSceneSamples           = false;   // whether to use the sampling settings from the rdla or user-specified settings
    bool mDirectRaysOn              = true;    // whether to display direct occlusion rays (bsdf + light)
    bool mIndirectRaysOn            = true;    // whether to display the indirect, continuing rays
    bool mSamplesOn                 = false;   // whether to display the diffuse + specular + light samples
    bool mIndirectSpecularRaysOn    = true;    // whether to display specular rays
    bool mIndirectDiffuseRaysOn     = true;    // whether to display diffuse rays
    bool mDirectSpecularRaysOn      = true;    // whether to display occlusion rays sampled from a specular bsdf
    bool mDirectDiffuseRaysOn       = true;    // whether to display occlusion rays sampled from a diffuse bsdf
    bool mDirectLightRaysOn         = true;    // whether to display occlusion rays sampled from the light
    bool mDiffuseSamplesOn          = true;    // whether to display the directions of all the diffuse samples
    bool mSpecularSamplesOn         = true;    // whether to display the directions of all the specular samples
    bool mLightSamplesOn            = true;    // whether to display the directions of all the light samples
    scene_rdl2::math::Color mCameraRayColor             = scene_rdl2::math::Color(0, 0, 1);
    scene_rdl2::math::Color mIndirectSpecularRayColor   = scene_rdl2::math::Color(0, 0.74, 1);
    scene_rdl2::math::Color mIndirectDiffuseRayColor    = scene_rdl2::math::Color(1, 0, 1);
    scene_rdl2::math::Color mDirectSpecularRayColor     = scene_rdl2::math::Color(0.11, 1, 0.27);
    scene_rdl2::math::Color mDirectDiffuseRayColor      = scene_rdl2::math::Color(0.83, 0.64, 0.83);
    scene_rdl2::math::Color mDirectLightRayColor        = scene_rdl2::math::Color(1, 1, 0);
    float mLineWidth = 1.0f;             // width of the lines drawn
    float mMaxRayLength = 1000.f;        // ray length is capped at this value
    float mHiddenLineOpacity = 0.2f;     // opacity of the line segments that are behind a scene object

    void setPixelRange(const int xMin, const int yMin, const int xMax, const int yMax)
    {
        mPixelXmin = xMin;
        mPixelYmin = yMin;
        mPixelXmax = xMax;
        mPixelYmax = yMax;
    }

    Parser& getParser() { return mParser; }

    std::string show() const;
    std::string showPixel() const;

private:
    void parserConfigure();

    bool mOn {false};

    int mPixelXmin {0};
    int mPixelYmin {0};
    int mPixelXmax {0};
    int mPixelYmax {0};

    Parser mParser;
};

/// --------------------------------------------------------------------------------------------------------------------

/// Current state
enum class State : uint8_t {
    NONE,                    // does not yet contain any data, hasn't been initialized
    READY,                   // has been initialized
    START_RECORD,            // flag to start recording
    RECORD,                  // is currently recording data (rendering in debug mode)
    STOP_RECORD,             // done recording data -- needs to stop rendering
    REQUEST_DRAW,            // request that we create and draw the visualization
    GENERATE_LINES,          // create line segments that we will use to draw
    DRAW                     // visualization is ready to draw
};

/// --------------------------------------------------------------------------------------------------------------------
    
/// The PathVisualizer class manages the gathering and drawing of ray information 
/// on top of the render buffer. It is managed at a high level from the PathVisualizerManager
/// class, which determines what state the visualizer is in, and what functions are performed.
/// It can be broken up into three stages:
///
///     STAGE 1: Data Gathering
///         During this stage, we interrupt any ongoing rendering process to kick off
///         a render in "simulation mode" (this happens during renderPrep for moonray_gui and
///         inside the arras event loop for the arras/moonray context).
///         This shrinks the viewport to the user-selected pixel, and sets the sampling parameters
///         according to the user selections as well (see RenderContext:startFrame). Then, we call recordRay()
///         whenever a ray is generated during rendering to save the ray data in the form of a PathVisualizerNode.
///         Any additional filtering occurs during this stage as well, so only PathVisualizerNodes that match the user
///         parameters are ultimately added.
///
///     STAGE 2: Generating Line Segments
///         Once we have gathered all ray data, the next step is to turn that ray data into line segments that can be
///         drawn. This is a computation-heavy step, as we need to clip the world-space points, compute their depths,
///         and project them to screen-space. Afterwards, we also need to trace the line, running an occlusion test at 
///         each pixel to determine whether it is occluded. If it is, we end the current line in order to generate a line
///         that is entirely occluded. This allows us to set a different transparency for all occluded portions of rays.
///         Since this step is so computation-heavy, we would prefer to perform this once after gathering ray data,
///         to avoid having to do it every time we draw. After the data gathering stage, this function shouldn't be 
///         called until the next data gathering stage, unless the camera is moved, which affects the world-to-screen
///         transformation and occlusion tests. The client is responsible for ensuring this function is called efficiently,
///         and at the appropriate times.
///
///     STAGE 3: Draw
///         This is the simplest stage. During this stage, we take all line segments, and use a line-drawing algorithm
///         to draw on top of the given render buffer. The client is responsible for calling this function every frame,
///         once we are in the DRAW state. This function is currently only executed in moonray_gui, as the arras/moonray
///         context uses the ClientReceiverFb's telemetry functionality (SEE: mcrt_dataio/lib/ClientReceiverFb.cc).

class PathVisualizer {

public:

    /// -------------------------------------------------------------------------------------------

    /// Type flags for a Node
    /// All rays (except camera rays) will have an origin type and a ray type
    ///
    /// A "direct" ray is an occlusion ray, which can either be produced from a light sample
    /// or a bsdf sample. The ray is only added if it has a chance of hitting the light, even if it is
    /// occluded. If it is occluded, that is taken into account when we draw the rays.
    ///
    /// An "indirect" ray is a continuing ray that is produced from a bsdf lobe, so it will either be 
    /// DIFFUSE or SPECULAR. 
    /// 
    /// A "sample" is any sample that is taken that has not yet been evaluated (i.e. it hasn't been used to 
    /// generate a ray, and hasn't been checked for validity). These "samples" are visualized as rays, but with a set
    /// length. This allows us to evaluate our sampling code.
    enum Flags : uint8_t {
        /* Origin/Lobe Type */
        CAMERA          = 1 << 0, // camera ray
        DIFFUSE         = 1 << 1, // diffuse ray
        SPECULAR        = 1 << 2, // specular ray
        LIGHT           = 1 << 3, // light ray/sample

        /* Ray Type */
        DIRECT          = 1 << 4, // direct lighting ray (occlusion ray)
        INDIRECT        = 1 << 5, // indirect ray (continuing ray)
        SAMPLE          = 1 << 6, // sample (does not have an endpoint)

        /* Line Status */
        INACTIVE        = 1 << 7, // the part of a ray that has become inactive (ex: after a light ray hits an occluder)
    };

    // 7 6 5 4 3 2 1 0
    // ||<-+->|<--+-->|
    // |   |      |
    // |   |      +------ OriginType (OriginTypeMask)
    // |   +------------- RayType (RayTypeMask)
    // +----------------- LineStatus

    static constexpr unsigned OriginTypeMask = 0x0f;
    static constexpr unsigned RayTypeMask = 0x70;
    static constexpr unsigned LineStatusBit = 0x80;

    static void setOriginType(Flags& flags, const Flags originType)
    {
        uint8_t flags8 = static_cast<uint8_t>(flags) & ~OriginTypeMask;
        flags = static_cast<Flags>((flags8) | static_cast<uint8_t>(originType));
    }

    static void setRayType(Flags& flags, const Flags rayType)
    {
        uint8_t flags8 = static_cast<uint8_t>(flags) & ~RayTypeMask;
        flags = static_cast<Flags>((flags8) | static_cast<uint8_t>(rayType));
    }

    static void setLineStatus(Flags& flags, const Flags lineStatus)
    {
        uint8_t flags8 = static_cast<uint8_t>(flags) & ~LineStatusBit;
        flags = static_cast<Flags>((flags8) | static_cast<uint8_t>(lineStatus));
    }

    static bool matchesOriginType(const Flags flags, const Flags originType)
    {
        uint8_t flags8 = static_cast<uint8_t>(flags) & OriginTypeMask;
        return flags8 & static_cast<uint8_t>(originType);
    }

    static bool matchesRayType(const Flags flags, const Flags rayType)
    {
        uint8_t flags8 = static_cast<uint8_t>(flags) & RayTypeMask;
        return flags8 & static_cast<uint8_t>(rayType);
    }

    static bool matchesLineStatus(const Flags flags, const Flags lineStatus)
    {
        uint8_t flags8 = static_cast<uint8_t>(flags) & LineStatusBit;
        return flags8 & static_cast<uint8_t>(lineStatus);
    }

    /// -------------------------------------------------------------------------------------------

    // A Node stores all the information about a ray needed for visualization
    struct Node {
        int mRayOriginIndex;                            // index of ray origin point in mVertices (world space)
        int mRayEndpointIndex;                          // index of ray endpoint in mVertices (world space)
        int mRayIsectIndex;                             // index of where the ray intersects (-1 if not occlusion ray)
        int8_t mDepth;                                  // ray depth
        Flags mFlags;                                   // node type flags

        Node(int rayOriginIndex, int rayEndpointIndex, int rayIsectIndex, int rayDepth, Flags flags)
                : mRayOriginIndex(rayOriginIndex), 
                  mRayEndpointIndex(rayEndpointIndex), 
                  mRayIsectIndex(rayIsectIndex),
                  mDepth(rayDepth), 
                  mFlags(flags) {}

        Node() = default;
        ~Node() = default;

        bool isType(const Flags flag) const { return mFlags == flag; }
    };

    // Pixel coords should be unsigned
    // when line segments are created
    struct PixelCoordU {
        uint32_t x;
        uint32_t y;
    };

    // Pixel coords might be negative
    // during transformation & drawing processes
    struct PixelCoordI {
        int x;
        int y;
    };

    using PosType = scene_rdl2::grid_util::VectorPacketLineStatus::PosType;

    struct LineSegment {
        size_t mNodeIndex {0};      // index of the node this line segment belongs to
        PixelCoordU mPx1;           // starting pixel
        PixelCoordU mPx2;           // ending pixel
        Flags mFlags;               // type of line to draw
        bool mDrawEndpoint;         // whether the line should have an endpoint
        float mAlpha;               // transparency of line

        PosType mStartPosType {PosType::UNKNOWN};
        PosType mEndPosType {PosType::UNKNOWN};
    };

    /// -------------------------------------------------------------------------------------------

    using Arg = scene_rdl2::grid_util::Arg;
    using Parser = scene_rdl2::grid_util::Parser; 

    PathVisualizer();
    ~PathVisualizer();

    static scene_rdl2::grid_util::VectorPacketLineStatus::RayType flagsToRayType(const uint8_t& flags);
    static scene_rdl2::grid_util::VectorPacketLineStatus::RayType flagsToRayType(const Flags& flags);
    scene_rdl2::math::Color getColorByFlags(const uint8_t& flags) const;

    void setOn(const bool flag) { mOn = flag; }

    /// Initializes the visualizer
    void initialize(const unsigned int width, const unsigned int height, 
                    const PathVisualizerParams* params);

    // Calls recordRay() to record a camera ray
    void recordCameraRay(const mcrt_common::Ray& ray, const Scene& scene, const uint32_t pixel);
    // Calls recordRay() to record an indirect diffuse/specular ray
    void recordIndirectRay(const mcrt_common::Ray& ray, const Scene& scene, const uint32_t pixel, const int lobeType);
    // Calls recordRay() to record a direct diffuse/specular occlusion ray
    void recordDirectBsdfRay(const mcrt_common::Ray& ray, const Scene& scene, const uint32_t pixel, 
                             const int lobeType, const bool occludedFlag);
    // Calls recordRay() to record a direct light occlusion ray
    void recordDirectLightRay(const mcrt_common::Ray& ray, const Scene& scene, 
                              const uint32_t pixel, const bool occludedFlag);
    // Calls recordRay() to record a diffuse/specular sample
    void recordBsdfSample(const mcrt_common::Ray& ray, const Scene& scene, const uint32_t pixel, const int lobeType);
    // Calls recordRay() to record a light sample
    void recordLightSample(const mcrt_common::Ray& ray, const Scene& scene, const uint32_t pixel);

    /// Create LineSegments that we can use to draw.
    /// This function should be called once after gathering ray data, and
    /// before we enter the DRAW state. It should also be called if the camera
    /// has moved, since that affects the world-to-screen transformation and
    /// occlusion tests.
    void generateLines(const Scene* scene);
    void resetLines() { mLines.clear(); } // not MTsafe

    /// Draws the path visualization with the given user parameters.
    /// This function should be called every frame, once we are in the DRAW state.
    ///
    /// We used to do all of the line processing (occlusion tests, clipping, etc) during this
    /// function, but that was too slow to do every frame. Now, we've split it up into two steps:
    /// 1. Generate the line segments
    /// 2. Draw the line segments
    /// This way, we don't incur the cost of generating lines every frame, and certain parameters
    /// (line width, colors) can be changed without having to regenerate the lines.
    void draw(scene_rdl2::fb_util::RenderBuffer* renderBuffer, const Scene* scene);

    template <typename F>
    void crawlAllLines(F callBack) {
        for (const LineSegment& line: mLines) {
            callBack(scene_rdl2::math::Vec2i(line.mPx1.x, line.mPx1.y),
                     scene_rdl2::math::Vec2i(line.mPx2.x, line.mPx2.y),
                     static_cast<const uint8_t>(line.mFlags),
                     line.mAlpha,
                     mParams->mLineWidth,
                     line.mDrawEndpoint,
                     line.mNodeIndex,
                     line.mStartPosType,
                     line.mEndPosType);
        }
    };

    size_t getTotalLines() const { return mLines.size(); }

    /// Clears all ray data
    void reset();

    /// Returns the amount of memory used, in bytes
    size_t getMemoryFootprint() const;

    // Returns the number of nodes
    size_t getNodeCount() const { return mNodes.size(); }

    /// Gets/sets the current state of the visualizer
    const State& getState() const { return mState; }
    void setState(State state);

    /// Gets/sets whether we need to restart rendering
    /// once we are done gathering data
    bool getNeedsRenderRefresh() const { return mNeedRenderRefresh; }
    void setNeedsRenderRefresh(bool refresh) 
    {
        std::lock_guard<std::mutex> lock(mWriteLock);
        mNeedRenderRefresh = refresh; 
    }

    size_t getLightSampleRayCount() const { return mLightSampleRayCount; }
    size_t getBsdfSampleRayCount() const { return mBsdfSampleRayCount; }
    size_t getDiffuseRayCount() const { return mDiffuseRayCount; }
    size_t getSpecularRayCount() const { return mSpecularRayCount; }

    /// Print how long everything took
    void printTimeStats() const;

    // Print ALL of the Nodes, up to maxEntries
    // If maxEntries == -1, print all nodes
    void printNodes(const int maxEntries) const;

    /// Given a Node index, print the node
    void printNode(const size_t nodeIndex) const;

    /// Given a Node index, return the node information as a string
    std::string getNodeInfo(const size_t nodeIndex) const;

    bool getCamPos(scene_rdl2::math::Vec3f& camPos) const;

    // Returns all the camera ray intersection points with the surface.
    std::vector<scene_rdl2::math::Vec3f> getCamRayIsectSfPos() const;

    size_t serializeNodeDataAll(std::string& buff) const;

    /// debug console command parser
    Parser& getParser() { return mParser; }

    static bool isCameraRay(const Flags flags);
    static bool isIndirectSpecularRay(const Flags flags);
    static bool isIndirectDiffuseRay(const Flags flags);
    static bool isDirectSpecularRay(const Flags flags);
    static bool isDirectDiffuseRay(const Flags flags);
    static bool isDirectLightRay(const Flags flags);
    static bool isDiffuseSample(const Flags flags);
    static bool isSpecularSample(const Flags flags);
    static bool isLightSample(const Flags flags);

private:
    /// Sets up the viewing frustum, mFrustum, for the given camera
    bool setUpFrustum(const Camera& cam);

    /// Creates a new Node to store all of the given ray information
    void recordRay(const mcrt_common::Ray& ray, const Scene& scene, const uint32_t pixel,
                   Flags flags, const bool occlusionFlag);

    /// Given some ray data, check whether that ray matches the user parameters
    bool matchesParams(const Flags flags, const int depth) const;

    /// Check if the current pixel is occluded by scene geometry
    bool pixelIsOccluded(const PixelCoordU& p, const PixelCoordI& p1, 
                         const pbr::Scene* scene, const float totalDistance,
                         const float invDepth1, const float invDepthDiff) const;
    
    /// Find the ray origin, endpoint, and intersection, if it's an occlusion ray. Then,
    /// clip those points using the viewing frustum. Returns the number of elements 
    /// in outPoints (can be 2-3)
    uint8_t clipPoints(const size_t nodeIndex, scene_rdl2::math::Vec3f* outPoints, bool* clipStatus) const;

    using OcclusionFunction = std::function<bool(const PixelCoordU& p)>;

    /// A line-drawing algorithm that creates line segments
    /// Some lines will be split into multiple segments if they 
    /// have portions that are occluded
    void traceLine(const size_t nodeIndex,
                   const PosType startPosType,
                   const PosType endPosType,
                   const PixelCoordI& start, const PixelCoordI& end,                    
                   const OcclusionFunction& occlusionFunc,
                   const Flags flag, const bool endpointClipped);

    /// Given a node, this function transforms the ray endpoints and 
    /// creates 2D line segment(s), which can then be drawn in the draw() function.
    void generateLine(const size_t nodeIndex, const Scene* scene);

    /// Adds a new 2D line segment
    void addLineSegment(const size_t nodeIndex,
                        const PosType startPosType,
                        const PosType endPosType,
                        const PixelCoordU& start, const PixelCoordU& end, const Flags& flags, 
                        const bool drawEndpoint, const bool isOccluded);

    /// ---------- Utilities -----------------------------------------------------------------
    
    /// Finds the world-space intersection of the given ray with the scene
    scene_rdl2::math::Vec3f findSceneIsect(const mcrt_common::Ray& ray, const Scene& scene) const;

    /// Transform given world-space point to screen space
    scene_rdl2::math::Vec2f transformPointWorld2Screen(const scene_rdl2::math::Vec3f& p, const pbr::Camera* cam) const;

    /// ---- Getters ----

    /// Tells us whether the lobeTypes match
    inline bool matchesLobeType(const int lobeType1, const int lobeType2) const;

    /// Tells us whether the node at nodeIndex matches the current user-specified flags in mParams
    inline bool matchesFlags(const Flags flags) const;

    inline bool lobeIsDiffuse(const int lobeType) const;
    inline bool lobeIsSpecular(const int lobeType) const;

    /// Gets the depth of the ray
    inline int getRayDepth(const size_t nodeIndex) const;

    /// Does ray depth == 0?
    inline bool isCameraRay(const size_t nodeIndex) const;

    /// Gets the ray origin for the given node
    inline scene_rdl2::math::Vec3f getRayOrigin(const size_t nodeIndex) const;

    /// Gets the ray endpoint for the given node
    inline scene_rdl2::math::Vec3f getRayEndpoint(const size_t nodeIndex) const;

    /// Gets the ray isect for the given node
    /// Returns a zero vector if there is no isect for the node
    inline scene_rdl2::math::Vec3f getRayIsect(const size_t nodeIndex) const;

    /// Given the node index, returns a color based on ray type
    inline scene_rdl2::math::Color getRayColor(const Flags& flags) const;

    /// Have we added any nodes to the visualizer yet?
    inline bool isEmpty() const { return mNodes.size() == 0; }

    /// Checks that the given pixel is in the image bounds
    inline bool isInBounds(const PixelCoordU& p) const
    { 
        return p.x < mWidth && p.y < mHeight; 
    }
    inline bool isInBounds(const PixelCoordI& p) const
    { 
        return p.x >= 0 && p.x < mWidth && p.y >= 0 && p.y < mHeight; 
    }

    /// ---- Setters ----

    /// Resets the camera ray intersection index
    inline void resetCameraIsectIndex() { mCameraIsectIndex = -1; }

    /// Add the vertex to the vertex list and return its index
    inline int addVertex(const scene_rdl2::math::Vec3f& v);

    // Create a new node and add it to the nodes list
    inline void addNode(const int originIndex, const int endpointIndex, const int isectIndex, 
                        const int depth, const Flags& flags);

    /// -----------------

    // Given a list of Node indices, print them
    void printNodes(const std::vector<size_t>& filteredList) const;

    // Node type specified version
    template <typename F>
    void crawlAllNodes(const Flags flags, F callBack) const {
        for (const Node& node: mNodes) {
            if (node.isType(flags)) {
                if (!callBack(node)) break;
            }
        }
    };

    // a generic version that takes a predicate (= typeFunc)
    template <typename F>
    void crawlAllNodes(const std::function<bool(const Node&)>& typeFunc, F callBack) const {
        for (const Node& node: mNodes) {
            if (!typeFunc || typeFunc(node)) {
                if (!callBack(node)) break;
            }
        }
    };

    void parserConfigure();
    std::string showFlowCtrlState() const;
    std::string showLinesInfo() const;
    std::string showNodeInfo() const;
    std::string showCamPos() const;
    std::string showCamRayIsectSfPos() const;
    static std::string stateStr(const State state);

    /// ---------------------- Member variables --------------------------------

    bool mOn {false};

    /// All of the Nodes (path vertices) for the given viewport
    std::vector<Node> mNodes;

    /// All of the generated LineSegments
    std::vector<LineSegment> mLines;

    /// A list of path vertices, intended to reduce some duplicate vertices
    std::vector<scene_rdl2::math::Vec3f> mVertexBuffer;

    /// A read-only pointer to the user parameters, set in the PathVisualizerManager
    const PathVisualizerParams* mParams;

    /// What state is the visualizer in?
    State mState;

    /// Width/height of the render buffer
    unsigned int mWidth;
    unsigned int mHeight;

    /// Whether we need to restart rendering after gathering ray data
    bool mNeedRenderRefresh = false;

    /// The index in mVertices of a camera ray's intersection with the scene.
    /// Used to draw the pixel focus
    mutable int mCameraIsectIndex = -1;

    /// The camera's viewing frustum
    std::unique_ptr<mcrt_common::Frustum> mFrustum;

    /// Mutex to avoid simultaneous writes by different threads
    mutable std::mutex mWriteLock;

    /// Timing statistics
    mutable moonray::util::AverageDouble mInRenderingTime;
    mutable moonray::util::AverageDouble mPostRenderingTime;

    /// debug console command parser
    Parser mParser;    

    // Keep track of some stats
    mutable size_t mLightSampleRayCount;
    mutable size_t mBsdfSampleRayCount;
    mutable size_t mDiffuseRayCount;
    mutable size_t mSpecularRayCount;
};

} // end namespace rndr
} // end namespace moonray
