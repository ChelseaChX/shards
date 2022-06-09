// Tests for editor-related functionality
// such as gizmos,highlights,debug lines

#include "./context.hpp"
#include "./data.hpp"
#include "./renderer.hpp"
#include "renderer_utils.hpp"
#include <catch2/catch_all.hpp>
#include <gfx/context.hpp>
#include <gfx/drawable.hpp>
#include <gfx/features/wireframe.hpp>
#include <gfx/helpers/shapes.hpp>
#include <gfx/helpers/wireframe.hpp>

using namespace gfx;

static constexpr float comparisonTolerance = 0.05f;

TEST_CASE("Wireframe", "[Editor]") {
  auto testRenderer = createTestRenderer();
  Renderer &renderer = *testRenderer->renderer.get();

  MeshPtr cubeMesh = createCubeMesh();

  ViewPtr view = std::make_shared<View>();
  view->view = linalg::lookat_matrix(float3(3, 3.0f, 3.0f), float3(0, 0, 0), float3(0, 1, 0));
  view->proj = ViewPerspectiveProjection{
      degToRad(45.0f),
      FovDirection::Horizontal,
  };

  DrawQueuePtr queue = std::make_shared<DrawQueue>();
  DrawQueuePtr editorQueue = std::make_shared<DrawQueue>();
  float4x4 transform;
  DrawablePtr drawable;

  transform = linalg::translation_matrix(float3(0.0f, 0.0f, 0.0f));
  drawable = std::make_shared<Drawable>(cubeMesh, transform);
  drawable->parameters.set("baseColor", float4(0.2f, 0.2f, 0.2f, 1.0f));

  PipelineSteps steps{
    makeDrawablePipelineStep(RenderDrawablesStep{
        .drawQueue = queue,
        .features =
            {
                features::Transform::create(),
                features::BaseColor::create(),
            },
    }),
        makeDrawablePipelineStep(RenderDrawablesStep{
          .drawQueue = editorQueue,
          .features =
              {
                  features::Transform::create(),
                  features::BaseColor::create(),
              },
      }),
  };

  WireframeRenderer wr0(false);
  WireframeRenderer wr1(true);

  auto loop = [&](WireframeRenderer &wr) {
    queue->clear();
    editorQueue->clear();

    queue->add(drawable);
    wr.overlayWireframe(editorQueue, drawable);

    renderer.render(view, steps);
  };

  TEST_RENDER_LOOP(testRenderer) { loop(wr0); };
  CHECK(testRenderer->checkFrame("wireframe", comparisonTolerance));

  TEST_RENDER_LOOP(testRenderer) { loop(wr1); };
  CHECK(testRenderer->checkFrame("wireframe-backfaces", comparisonTolerance));
}

TEST_CASE("Shapes", "[Editor]") {
  auto testRenderer = createTestRenderer();
  Renderer &renderer = *testRenderer->renderer.get();

  ViewPtr view = std::make_shared<View>();
  view->view = linalg::lookat_matrix(float3(3.0f, 3.0f, 3.0f), float3(0, 0, 0), float3(0, 1, 0));
  view->proj = ViewPerspectiveProjection{
      degToRad(45.0f),
      FovDirection::Horizontal,
  };

  MeshPtr cubeMesh = createCubeMesh();
  float4x4 transform;
  DrawablePtr cubeDrawable;

  transform = linalg::translation_matrix(float3(0.0f, 0.0f, 0.0f));
  cubeDrawable = std::make_shared<Drawable>(cubeMesh, transform);
  cubeDrawable->parameters.set("baseColor", float4(0.2f, 0.2f, 0.2f, 1.0f));

  DrawQueuePtr baseQueue = std::make_shared<DrawQueue>();
  DrawQueuePtr editorQueue = std::make_shared<DrawQueue>();

  PipelineSteps steps{
      makeDrawablePipelineStep(RenderDrawablesStep{
          .drawQueue = baseQueue,
          .features =
              {
                  features::Transform::create(),
                  features::BaseColor::create(),
              },
      }),
      makeDrawablePipelineStep(RenderDrawablesStep{
          .drawQueue = editorQueue,
          .features =
              {
                  ScreenSpaceSizeFeature::create(),
                  features::BaseColor::create(),
              },
      }),
  };

  ShapeRenderer sr;
  TEST_RENDER_LOOP(testRenderer) {
    baseQueue->clear();
    editorQueue->clear();

    // Cube to test depth buffer interaction
    baseQueue->add(cubeDrawable);

    sr.begin();
    size_t numSteps = 8;
    float spacing = 1.0f / 8.0f;
    for (size_t i = 0; i < numSteps; i++) {
      float sz = (float(i) /* - float(numSteps) / 2.0f - 0.5f */) * spacing;
      sr.addLine(float3(0, 0, sz), float3(1, 0, sz), float4(1, 0, 0, 1), 1 + i);
      sr.addLine(float3(0, 0, sz), float3(1, 0, sz), float4(1, 0, 0, 1), 1 + i);

      sr.addLine(float3(sz, 0, 0), float3(sz, 1, 0), float4(0, 1, 0, 1), 1 + i);
      sr.addLine(float3(sz, 0, 0), float3(sz, 1, 0), float4(0, 1, 0, 1), 1 + i);

      sr.addLine(float3(0, sz, 0), float3(0, sz, 1), float4(0, 0, 1, 1), 1 + i);
      sr.addLine(float3(0, sz, 0), float3(0, sz, 1), float4(0, 0, 1, 1), 1 + i);
    }
    sr.finish(editorQueue);
    renderer.render(view, steps);
  };

  CHECK(testRenderer->checkFrame("helper_lines", comparisonTolerance));
}
