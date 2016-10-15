#pragma once

#include <cstdint>

#include "../simplemath/aabb.h"

namespace Export
{
	// Geometry data file
	enum FormatComponentType
	{
		FCT_END = 0,

		FCT_FLOAT2,
		FCT_FLOAT3,
		FCT_FLOAT4,

		FCT_INT16_2N,
		FCT_INT16_4N,

		FCT_UINT8_4,
	};

	struct GeometryInfo
	{
		uint32_t version;

		uint32_t formatComponents;

		uint32_t vertexCount;
		uint32_t vertexSize;

		uint32_t indexCount;
		uint32_t indexSize;

		aabb bounds;

		// uint32_t vertexFormat[formatComponents];

		// uint8_t vertexData[vertexCount][vertexSize];

		// uint8_t indexData[indexCount][indexSize];
	};

	// Object data file
	struct NodeInfo
	{
		uint32_t nameOffset; // Offset into the string data
		uint32_t geometryNameOffset; // Offset into the string data

		uint32_t parentID;		// Reference to parent node
		uint32_t controllerID;	// Reference to controller
		uint32_t effectID;		// Reference to material information
		uint32_t flags;

		mat4 model;			// Model matrix with parent transformations
		mat4 modelOriginal;	// Model matrix without parent transformations
	};

	struct ControllerInfo
	{
		uint32_t boneCount;

		mat4 bindPose;

		// uint32_t nodeIDs[boneCount];
		// mat4 invBindMats[boneCount];
		// aabb boneBounds[boneCount];
	};

	struct MaterialInfo
	{
		uint32_t colorStringOffset; // Offset into the string data
		uint32_t alphaStringOffset; // Offset into the string data
	};

	struct MeshInfo
	{
		uint32_t header; // 0x57bedefe
		uint32_t version;

		// uint32_t nodeCount;
		// NodeInfo nodes[nodeCount];
		
		// uint32_t controllerCount;
		// ControllerInfo controllers[controllerCount];

		// uint32_t animNodeCount;
		// uint32_t animNodeIds[animNodeCount];

		// uint32_t animSampleCount;
		// mat4 animSamples[animSampleCount][animNodeCount];

		// aabb aabbState[animSampleCount ? animSampleCount : 1];

		// uint32_t materialCount;
		// MaterialInfo materials[materialCount];

		// uint32_t stringDataSize;
		// char stringData[stringDataSize];
	};

	// To prepare transformations for rendering:
	// - iterate over all nodes, multiply parent transform by modelOriginal
	// - iterate over all nodes and find if a node references a controller, in which case, each bone transformation is the node transform multiplied by inverse bind matrix
}
