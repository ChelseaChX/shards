#ifndef D0FA649B_29CC_4C2E_8A09_F3142788BE28
#define D0FA649B_29CC_4C2E_8A09_F3142788BE28

#include "../drawable.hpp"
#include "../feature.hpp"
#include "../linalg.hpp"
#include "../mesh.hpp"
#include "../shader/blocks.hpp"

namespace gfx {
struct ShapeVertex {
  float position[3];
  float color[4] = {1, 1, 1, 1};
  float direction[3] = {};
  float fdata[1] = {0};
  uint32_t udata[1] = {0};

  void setPosition(const float3 &position) { memcpy(this->position, &position.x, sizeof(float) * 3); }
  void setColor(const float4 &color) { memcpy(this->color, &color.x, sizeof(float) * 4); }
  void setNormal(const float3 &direction) { memcpy(this->direction, &direction.x, sizeof(float) * 3); }
  void setFloatData(float fdata) { this->fdata[0] = fdata; }
  void setFloatData(uint32_t udata) { this->udata[0] = udata; }

  static const std::vector<MeshVertexAttribute> &getAttributes() {
    static std::vector<MeshVertexAttribute> attribs = []() {
      std::vector<MeshVertexAttribute> attribs;
      attribs.emplace_back("position", 3, VertexAttributeType::Float32);
      attribs.emplace_back("color", 4, VertexAttributeType::Float32);
      attribs.emplace_back("direction", 3, VertexAttributeType::Float32);
      attribs.emplace_back("fdata", 1, VertexAttributeType::Float32);
      attribs.emplace_back("udata", 1, VertexAttributeType::UInt32);
      return attribs;
    }();
    return attribs;
  }
};

struct ScreenSpaceSizeFeature {
  static inline FeaturePtr create() {
    FeaturePtr result = std::make_shared<Feature>();
    result->state.set_culling(false);

    shader::EntryPoint &entry =
        result->shaderEntryPoints.emplace_back("screenSpaceLineGeometry", ProgrammableGraphicsStage::Vertex, shader::BlockPtr());

    using namespace gfx::shader::blocks;
    using namespace gfx::shader;
    std::unique_ptr<Compound> code = makeCompoundBlock();
    code->appendLine("var width = f32(", ReadInput("udata"), ")");
    code->appendLine("var lineY =", ReadInput("fdata"));
    code->appendLine("var dir = ", ReadInput("direction"));
    code->appendLine("var color = ", ReadInput("color"));
    code->appendLine("var posWS = ", ReadInput("position"));
    code->appendLine("var world = ", ReadBuffer("world", FieldTypes::Float4x4));
    code->appendLine("var view = ", ReadBuffer("view", FieldTypes::Float4x4, "view"));
    code->appendLine("var invView = ", ReadBuffer("invView", FieldTypes::Float4x4, "view"));
    code->appendLine("var proj = ", ReadBuffer("proj", FieldTypes::Float4x4, "view"));
    code->appendLine("var viewport = ", ReadBuffer("viewport", FieldTypes::Float4, "view"));
    code->appendLine("var cameraPosition = invView[3].xyz");
    code->appendLine("var nextWS = posWS+", ReadInput("direction"));
    code->appendLine("var nextProj = proj* view * world * vec4<f32>(nextWS, 1.0)");
    code->appendLine("var nextNDC = nextProj.xyz / nextProj.w");
    code->appendLine("var posProj = proj* view * world * vec4<f32>(posWS, 1.0)");
    code->appendLine("var posNDC = posProj.xyz / posProj.w");
    code->appendLine("var directionSS = normalize(nextNDC.xy - posNDC.xy)");
    code->appendLine("var tangentSS = vec2<f32>(-directionSS.y, directionSS.x)");
    code->appendLine("var posSS = posNDC.xy * viewport.zw");
    code->appendLine("posProj.x = (posNDC.x + tangentSS.x * width * lineY * (1.0/viewport.z)) * posProj.w");
    code->appendLine("posProj.y = (posNDC.y + tangentSS.y * width * lineY * (1.0/viewport.w)) * posProj.w");
    code->append(WriteOutput("position", FieldTypes::Float4, "posProj"));
    entry.code = std::move(code);

    return result;
  }
};

struct ShapeRenderer {
private:
  std::vector<ShapeVertex> vertices;
  MeshPtr mesh;
  DrawablePtr drawable;

public:
#define UNPACK3(_x) \
  { _x.x, _x.y, _x.z }
#define UNPACK4(_x) \
  { _x.x, _x.y, _x.z, _x.w }
  void addLine(float3 a, float3 b, float4 color, uint32_t thickness) {
    float3 direction = linalg::normalize(b - a);
    vertices.push_back(ShapeVertex{
        .position = UNPACK3(a),
        .color = UNPACK4(color),
        .direction = UNPACK3(direction),
        .fdata = {1.0f},
        .udata = {thickness},
    });
    vertices.push_back(ShapeVertex{
        .position = UNPACK3(b),
        .color = UNPACK4(color),
        .direction = UNPACK3(direction),
        .fdata = {1.0f},
        .udata = {thickness},
    });
    vertices.push_back(ShapeVertex{
        .position = UNPACK3(b),
        .color = UNPACK4(color),
        .direction = UNPACK3(direction),
        .fdata = {-1.0f},
        .udata = {thickness},
    });

    vertices.push_back(ShapeVertex{
        .position = UNPACK3(a),
        .color = UNPACK4(color),
        .direction = UNPACK3(direction),
        .fdata = {-1.0f},
        .udata = {thickness},
    });
    vertices.push_back(ShapeVertex{
        .position = UNPACK3(a),
        .color = UNPACK4(color),
        .direction = UNPACK3(direction),
        .fdata = {1.0f},
        .udata = {thickness},
    });
    vertices.push_back(ShapeVertex{
        .position = UNPACK3(b),
        .color = UNPACK4(color),
        .direction = UNPACK3(direction),
        .fdata = {-1.0f},
        .udata = {thickness},
    });
  }

  void begin() { vertices.clear(); }

  void finish(DrawQueuePtr queue) {
    if (!mesh)
      mesh = std::make_shared<Mesh>();
    MeshFormat fmt = {
        .primitiveType = PrimitiveType::TriangleList,
        .windingOrder = WindingOrder::CCW,
        .vertexAttributes = ShapeVertex::getAttributes(),
    };
    mesh->update(fmt, vertices.data(), vertices.size() * sizeof(ShapeVertex), nullptr, 0);

    if (!drawable)
      drawable = std::make_shared<Drawable>(mesh);
    queue->add(drawable);
  }
};
#undef UNPACK3
#undef UNPACK4

struct GizmoRenderer {
  float4x4 view;

  void begin() {}
  void end() {}

  void append() {}
};
} // namespace gfx

#endif /* D0FA649B_29CC_4C2E_8A09_F3142788BE28 */
