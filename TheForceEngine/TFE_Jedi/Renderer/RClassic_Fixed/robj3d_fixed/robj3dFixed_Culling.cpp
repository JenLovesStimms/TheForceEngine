#include <TFE_System/profiler.h>
#include <TFE_Jedi/Math/fixedPoint.h>
#include <TFE_Jedi/Math/core_math.h>

#include "robj3dFixed_Culling.h"
#include "robj3dFixed_TransformAndLighting.h"
#include "../rclassicFixedSharedState.h"
#include "../../rcommon.h"

namespace TFE_Jedi
{

namespace RClassic_Fixed
{
	enum
	{
		POLYGON_FRONT_FACING = 0,
		POLYGON_BACK_FACING,
	};

	// List of potentially visible polygons (after backface culling).
	std::vector<JmPolygon*> s_visPolygons;

	s32 getPolygonFacing(const vec3_fixed* normal, const vec3_fixed* pos)
	{
		const vec3_fixed offset = { -pos->x, -pos->y, -pos->z };
		return dot(normal, &offset) < 0 ? POLYGON_BACK_FACING : POLYGON_FRONT_FACING;
	}

	s32 robj3d_backfaceCull(JediModel* model)
	{
		vec3_fixed* polygonNormal = s_polygonNormalsVS.data();
		s32 polygonCount = model->polygonCount;
		JmPolygon* polygon = model->polygons;
		for (s32 i = 0; i < polygonCount; i++, polygonNormal++, polygon++)
		{
			vec3_fixed* vertex = &s_verticesVS[polygon->indices[1]];
			polygonNormal->x -= vertex->x;
			polygonNormal->y -= vertex->y;
			polygonNormal->z -= vertex->z;
		}

		if (polygonCount > s_visPolygons.size())
		{
			s_visPolygons.resize(polygonCount * 2);
		}

		JmPolygon** visPolygon = s_visPolygons.data();
		s32 visPolygonCount = 0;

		polygon = model->polygons;
		polygonNormal = s_polygonNormalsVS.data();
		for (s32 i = 0; i < model->polygonCount; i++, polygon++, polygonNormal++)
		{
			vec3_fixed* pos = &s_verticesVS[polygon->indices[1]];
			s32 facing = getPolygonFacing(polygonNormal, pos);
			if (facing == POLYGON_BACK_FACING) { continue; }

			visPolygonCount++;
			s32 vertexCount = polygon->vertexCount;
			s32 zAve = 0;

			s32* indices = polygon->indices;
			for (s32 v = 0; v < vertexCount; v++)
			{
				zAve += s_verticesVS[indices[v]].z;
			}

			polygon->zAve = div16(zAve, intToFixed16(vertexCount));
			*visPolygon = polygon;
			visPolygon++;
		}

		return visPolygonCount;
	}

}}  // TFE_Jedi