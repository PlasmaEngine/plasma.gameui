#include <GameUiTest/GameUiTestPCH.h>

#include <Core/World/World.h>
#include <GameUiPlugin/Components/UiCanvasComponent.h>

namespace
{
  float TriangleArea(const plDynamicArray<plVec2>& triangles)
  {
    float fArea = 0.0f;
    for (plUInt32 i = 0; i + 2 < triangles.GetCount(); i += 3)
    {
      const plVec2 a = triangles[i + 1] - triangles[i];
      const plVec2 b = triangles[i + 2] - triangles[i];
      fArea += plMath::Abs(a.x * b.y - a.y * b.x) * 0.5f;
    }
    return fArea;
  }
} // namespace

PL_CREATE_SIMPLE_TEST_GROUP(GameUiShapes);

PL_CREATE_SIMPLE_TEST(GameUiShapes, PathFillRule)
{
  // a 10x10 square with a 4x4 square inside
  const char* szRing = "M 0,0 L 10,0 L 10,10 L 0,10 Z M 3,3 L 7,3 L 7,7 L 3,7 Z";

  PL_TEST_BLOCK(plTestBlock::Enabled, "Separate fills both subpaths")
  {
    plUiPath path;
    path.SetPathData(szRing);

    plVec2 vMin, vMax;
    PL_TEST_FLOAT(TriangleArea(path.GetTriangles(vMin, vMax)), 100.0f + 16.0f, 0.01f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Even-odd cuts the hole")
  {
    plUiPath path;
    path.SetPathData(szRing);
    path.SetFillRule(plUiPathFillRule::EvenOdd);

    plVec2 vMin, vMax;
    PL_TEST_FLOAT(TriangleArea(path.GetTriangles(vMin, vMax)), 100.0f - 16.0f, 0.01f);
    PL_TEST_VEC2(vMin, plVec2(0.0f), 0.001f);
    PL_TEST_VEC2(vMax, plVec2(10.0f), 0.001f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Contours for the stroke")
  {
    plUiPath path;
    path.SetPathData("M 0,0 L 10,0 L 10,10 Z M 0,20 L 10,20");

    plArrayPtr<const plUiPath::Contour> contours;
    const plDynamicArray<plVec2>& points = path.GetContourPoints(contours);
    PL_TEST_INT(contours.GetCount(), 2);
    PL_TEST_BOOL(contours[0].m_bClosed);
    PL_TEST_BOOL(!contours[1].m_bClosed);
    PL_TEST_INT(contours[1].m_uiPointCount, 2);
    PL_TEST_INT(points.GetCount(), 5);
  }
}

PL_CREATE_SIMPLE_TEST_GROUP(GameUiWorldSpace);

PL_CREATE_SIMPLE_TEST(GameUiWorldSpace, PanelAndRay)
{
  plWorldDesc desc("GameUiWorldSpace");
  plWorld world(desc);
  PL_LOCK(world.GetWriteMarker());

  plGameObjectDesc objectDesc;
  objectDesc.m_LocalPosition = plVec3(0, 0, 1.5f);
  plGameObject* pObject = nullptr;
  world.CreateObject(objectDesc, pObject);

  plUiCanvasComponent* pCanvas = nullptr;
  plUiCanvasComponent::CreateComponent(pObject, pCanvas);
  pCanvas->SetSpace(plUiCanvasSpace::World);
  pCanvas->SetWorldSize(plVec2(0.5f, 0.4f));
  pCanvas->SetPixelsPerMetre(560.0f);

  PL_TEST_BLOCK(plTestBlock::Enabled, "Canvas size follows size and density")
  {
    PL_TEST_VEC2(pCanvas->GetWorldCanvasSize(), plVec2(280.0f, 224.0f), 0.001f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Canvas to world")
  {
    const plMat4 canvasToWorld = pCanvas->ComputeCanvasToWorld(nullptr);

    // the centre of the canvas is the owner, the panel faces +X: -Y is right, -Z is down
    PL_TEST_VEC3(canvasToWorld.TransformPosition(plVec3(140.0f, 112.0f, 0.0f)), plVec3(0, 0, 1.5f), 0.0001f);
    PL_TEST_VEC3(canvasToWorld.TransformPosition(plVec3(0.0f, 0.0f, 0.0f)), plVec3(0, 0.25f, 1.7f), 0.0001f);
    PL_TEST_VEC3(canvasToWorld.TransformPosition(plVec3(280.0f, 224.0f, 0.0f)), plVec3(0, -0.25f, 1.3f), 0.0001f);
  }

  PL_TEST_BLOCK(plTestBlock::Enabled, "Pointer ray")
  {
    plVec2 vCanvasPos;
    float fDistance = 0.0f;

    // straight at the centre from 2 m in front
    PL_TEST_BOOL(pCanvas->IntersectRay(nullptr, plVec3(2.0f, 0.0f, 1.5f), plVec3(-1.0f, 0.0f, 0.0f), vCanvasPos, fDistance));
    PL_TEST_VEC2(vCanvasPos, plVec2(140.0f, 112.0f), 0.01f);
    PL_TEST_FLOAT(fDistance, 2.0f, 0.0001f);

    // 10 cm to the camera's right (-Y) and 5 cm up
    PL_TEST_BOOL(pCanvas->IntersectRay(nullptr, plVec3(2.0f, -0.1f, 1.55f), plVec3(-1.0f, 0.0f, 0.0f), vCanvasPos, fDistance));
    PL_TEST_VEC2(vCanvasPos, plVec2(140.0f + 56.0f, 112.0f - 28.0f), 0.01f);

    // beside the panel, and pointing away from it
    PL_TEST_BOOL(!pCanvas->IntersectRay(nullptr, plVec3(2.0f, 1.0f, 1.5f), plVec3(-1.0f, 0.0f, 0.0f), vCanvasPos, fDistance));
    PL_TEST_BOOL(!pCanvas->IntersectRay(nullptr, plVec3(2.0f, 0.0f, 1.5f), plVec3(1.0f, 0.0f, 0.0f), vCanvasPos, fDistance));
  }
}
