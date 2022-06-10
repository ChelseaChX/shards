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

  static const std::vector<MeshVertexAttribute> &getAttributes();
};

struct ScreenSpaceSizeFeature {
  static FeaturePtr create();
};

struct ShapeRenderer {
private:
  std::vector<ShapeVertex> vertices;
  MeshPtr mesh;
  DrawablePtr drawable;

public:
  void addLine(float3 a, float3 b, float3 dirA, float3 dirB, float4 color, uint32_t thickness);
  void addLine(float3 a, float3 b, float4 color, uint32_t thickness);
  void addCircle(float3 base, float3 xBase, float3 yBase, float radius, float4 color, uint32_t thickness, uint32_t resolution);
  void begin();
  void finish(DrawQueuePtr queue);
};

struct GizmoRenderer {
  float4x4 view;

  void begin() {}
  void end() {}

  void append() {}
};
} // namespace gfx

#endif /* D0FA649B_29CC_4C2E_8A09_F3142788BE28 */
