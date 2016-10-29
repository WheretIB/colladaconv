#pragma once

#include <cstdint>

#include <array>
#include <vector>

#include "../simplemath/aabb.h"

#pragma warning(disable: 4996)

#define LOG_VERBOSE
#define BONE_INFLUENCE_COUNT 3

struct IndexGroup
{
	static const uint32_t indGroups = 5;
	uint32_t indData[8];

	friend bool operator == (const IndexGroup& a, const IndexGroup& b)
	{
		for(uint32_t i = 0; i < indGroups; i++)
		{
			if(a.indData[i] != b.indData[i])
				return false;
		}

		return true;
	}
};

struct Vertex
{
	float pos[3];
	short tc[2];
	short normal[4];
};

struct NamedVertex
{
	NamedVertex(): id(0)
	{
	}

	NamedVertex(unsigned id, Vertex data): id(id), data(data)
	{
	}

	unsigned id;
	Vertex data;
};

enum StreamType
{
	ST_POSITION,
	ST_TEXCOORD,
	ST_NORMAL,
	ST_BINORMAL,
	ST_TANGENT,

	ST_COUNT
};

static const char *semanticName[] = {
	"POSITION",
	"TEXCOORD",
	"NORMAL",
	"TEXBINORMAL",
	"TEXTANGENT",
};

const uint32_t MaxStreams = 16u;

struct StreamInfo
{
	StreamInfo()
	{
		name = nullptr;

		count = 0;
		stride = 0;
		indexOffset = 0;

		data = nullptr;
	}

	const char *name;

	uint32_t count;
	uint32_t stride;
	uint32_t indexOffset;

	float *data;
};

class DAEGeometry
{
public:
	DAEGeometry()
	{
		ID = nullptr;
		name = nullptr;

		indices = nullptr;
		indCount = 0;

		std::fill(streamLink.begin(), streamLink.end(), ~0u);
	}

	void Free()
	{
		for(int i = 0; i < MaxStreams; i++)
			delete[] streams[i].data;
	}

	const char *ID;
	const char *name;

	std::array<StreamInfo, MaxStreams> streams;
	std::array<uint32_t, ST_COUNT> streamLink;

	IndexGroup *indices;
	uint32_t indCount;

	std::vector<Vertex> VB;
	std::vector<uint16_t> IB;
	std::vector<uint32_t> posIndex;

	aabb bounds;
};

enum DAETransformType
{
	DT_MATRIX,
	DT_ROTATE,
	DT_TRANSLATE,
	DT_SCALE,
	DT_LOOKAT,
	DT_SKEW,
};

struct DAETransform
{
	DAETransform()
	{
		sid = nullptr;
	}

	DAETransformType type;
	const char *sid;

	float data[16];
};

typedef DAETransform DAETransformBlock[8];

struct DAENode
{
	DAENode()
	{
		ID = nullptr;
		name = nullptr;
		sid = nullptr;

		tCount = 0;

		geometryID = ~0u;
		parentNodeID = ~0u;
		controllerID = ~0u;
		skeletonID = ~0u;
		effectID = ~0u;

		childCount = 0;
		isJoint = false;
		isSkeletonRoot = false;
	}

	const char *ID;
	const char *name;
	const char *sid;

	DAETransformBlock tForm;
	uint32_t tCount;
	mat4 model;

	uint32_t geometryID;
	uint32_t parentNodeID;
	uint32_t controllerID;
	uint32_t skeletonID;
	uint32_t effectID;

	uint32_t childCount;
	bool isJoint;
	bool isSkeletonRoot;
};

struct DAESkeleton
{
	DAESkeleton()
	{
		rootName = nullptr;

		controllerID = ~0u;

		jointCount = 0;
	}

	const char *rootName;
	uint32_t controllerID;

	mat4 bindShapeMat;
	uint32_t jointCount;
	std::array<uint32_t, 256> nodeIDs;
	std::array<mat4, 256> bindMat;
};

enum Interpolation
{
	LINEAR,
	BEZIER,
	STEP,

	I_UNSUPPORTED
};

struct DAESource
{
	DAESource()
	{
		ID = 0;

		dataNameBlob = 0;

		dataName = 0;

		isConstant = false;

		count = 0;
		stride = 0;
	}

	const char *ID;

	char *dataNameBlob;
	
	char **dataName;

	std::vector<float> dataFloat;

	bool isConstant;

	uint32_t count;
	uint32_t stride;
};

struct DAEAnimation
{
	const char *ID;

	DAESource *inSource, *outSource, *inTangentSource, *outTangentSource, *interpolationStr;
	Interpolation *interpolation;

	uint32_t dataCount;

	bool skip;

	char *targetNode;
	char *targetSID;
	char *targetComponent;

	uint32_t targetNodePos;
	uint32_t compOffset;

	uint32_t lastSample;
};

struct DAEController
{
	const char *ID, *source;
	uint32_t geometryID;

	mat4 bindMat;

	std::array<DAESource, 8> sources;
	uint32_t sourceCount;

	DAESource *joints, *binds, *weights;

	uint32_t vcountCount;	// Per vertex bone number information
	uint32_t *vcountData;	// Corresponding data
	uint32_t vCount;			// Index of the bone and weight index for every bone of every vertex
	int32_t *vData;			// Corresponding data

	bool	*isJointActive; // If joint accessed

	// How many joints are actually accessed and what are they
	unsigned	activeJointCount;
	unsigned	*activeJointIDs;
	unsigned	*jointRemap;

	aabb		*bounds; // For active joints

	// Export data
	int16_t *exWeights;	// 4 weight per vertex. Normalized value
	uint8_t	*exIndices;	// 4 bone index per vertex.
};

struct DAEImage
{
	const char *ID;
	const char *name;

	const char *path;
};

struct DAEEffect
{
	const char *ID;
	const char *name;

	unsigned diffuseColor;
	unsigned diffuseAlpha;
};

struct DAEMaterial
{
	const char *ID;
	const char *name;

	unsigned effectID;
};

struct Context
{
	Context(): geometryIDs(0), matrixAnimation(0)
	{
	}

	std::vector<DAEGeometry*> geoms;
	std::vector<DAENode> nodes;
	std::vector<DAESkeleton> skeletons;

	std::vector<unsigned> animatedNodes;
	mat4 *matrixAnimation;
	unsigned animSampleCount;

	std::vector<DAEController*> contrls;
	const char **geometryIDs;

	std::vector<DAEImage> images;
	std::vector<DAEEffect> effects;
	std::vector<DAEMaterial> materials;
};

struct ContextLocal
{
	std::vector<DAENode> nodes;
	std::vector<DAESkeleton> skeletons;
	std::vector<DAEEffect> effects;

	std::vector<unsigned> animatedNodes;
	mat4 *matrixAnimation;
	unsigned animSampleCount;
};
