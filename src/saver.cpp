#include <stdio.h>
#include <memory.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include <algorithm>

#include "../pugixml/src/pugixml.hpp"

#include "context.h"
#include "export.h"

void LogPrint(const char* format, ...);

#ifdef LOG_VERBOSE
#define LogOptional LogPrint
#else
#define LogOptional(...) (void)0
#endif

unsigned AppendString(std::vector<char> &stringData, const char *str)
{
	unsigned offset = stringData.size();

	stringData.insert(stringData.end(), str, str + strlen(str) + 1);

	return offset;
}

void SaveNode(char* fileNameOut, unsigned nodeID, Context &global)
{
	ContextLocal local;

	// Redirection tables
	std::vector<unsigned> parentRedirection;
	std::vector<unsigned> skeletonRedirection;
	std::vector<unsigned> effectRedirection;

	for(unsigned i = 0; i < global.nodes.size(); i++)
	{
		parentRedirection.push_back(~0u);
		skeletonRedirection.push_back(~0u);
		effectRedirection.push_back(~0u);
	}

	// Filter nodes and node data of interest
	std::vector<unsigned> allowedRoots;
	allowedRoots.push_back(nodeID);

	std::vector<unsigned> skeletonRoot;

	for(unsigned i = 0; i < global.nodes.size(); i++)
	{
		auto&& node = global.nodes[i];

		node.isSkeletonRoot = false;

		bool allowed = i == nodeID;

		for(unsigned k = 0; k < allowedRoots.size(); k++)
			allowed |= allowedRoots[k] == node.parentNodeID;

		bool isSkeletonRoot = false;

		for(unsigned k = 0; k < skeletonRoot.size(); k++)
			isSkeletonRoot |= skeletonRoot[k] == i;

		if(allowed || isSkeletonRoot)
		{
			parentRedirection[i] = local.nodes.size();

			local.nodes.push_back(node);

			if(isSkeletonRoot)
				local.nodes.back().isSkeletonRoot = true;

			allowedRoots.push_back(i);

			if(node.skeletonID != -1)
			{
				LogOptional("Allow nodes from skeleton %d\r\n", node.skeletonID);

				unsigned nodeID = global.skeletons[node.skeletonID].nodeIDs[0];

				for(;;)
				{
					if(global.nodes[nodeID].parentNodeID != ~0u)
						nodeID = global.nodes[nodeID].parentNodeID;
					else
						break;
				}

				LogOptional("    Skeleton node root is node %d\r\n", nodeID);

				skeletonRoot.push_back(nodeID);
			}
		}
	}

	LogPrint("Saving node %d (tree size %d out of %d)\r\n", nodeID, local.nodes.size(), global.nodes.size());

	std::vector<char> stringData;

	// Save file header
	FILE *fOut = fopen(fileNameOut, "wb");

	LogPrint("Saved file header\r\n");

	unsigned magic = 0x57bedefe;
	fwrite(&magic, 4, 1, fOut);

	unsigned version = 1;
	fwrite(&version, 4, 1, fOut);

	// Save nodes
	LogPrint("Saving nodes\r\n");

	unsigned nodeCount = (unsigned)local.nodes.size();
	fwrite(&nodeCount, 4, 1, fOut);

	Export::NodeInfo nodeInfo;

	std::vector<Export::NodeInfo> nodeList;
	nodeList.resize(local.nodes.size());

	std::vector<unsigned> nodeGeomList;
	nodeGeomList.resize(local.nodes.size());

	for(unsigned i = 0; i < local.nodes.size(); i++)
	{
		auto&& node = local.nodes[i];

		memset(&nodeInfo, 0, sizeof(Export::NodeInfo));

		LogOptional(" Node %d ", i);
		nodeInfo.nameOffset = AppendString(stringData, node.name);

		unsigned geomID = -1;

		if(node.controllerID != -1)
		{
			geomID = global.contrls[node.controllerID]->geometryID;

			LogOptional("references animated geometry %d through controller %d\r\n", geomID, node.controllerID);

			nodeInfo.geometryNameOffset = AppendString(stringData, global.geometryIDs[geomID]);
		}
		else if(node.geometryID != -1)
		{
			geomID = node.geometryID;

			LogOptional("references static geometry %d\r\n", geomID);

			nodeInfo.geometryNameOffset = AppendString(stringData, global.geometryIDs[geomID]);
		}
		else
		{
			LogOptional("holds transformation\r\n");
			nodeInfo.geometryNameOffset = 0;
		}

		if(node.skeletonID != -1)
		{
			LogOptional("  Uses skeleton %d\r\n", node.skeletonID);

			if(skeletonRedirection[node.skeletonID] == -1)
			{
				skeletonRedirection[node.skeletonID] = local.skeletons.size();
				local.skeletons.push_back(global.skeletons[node.skeletonID]);

				LogOptional("    Redirected to skeleton %d\r\n", skeletonRedirection[node.skeletonID]);

				auto &skeleton = local.skeletons.back();

				for(unsigned k = 0; k < skeleton.jointCount; k++)
				{
					LogOptional("        Redirected skeleton joint %d to %d\r\n", skeleton.nodeIDs[k], parentRedirection[skeleton.nodeIDs[k]]);

					skeleton.nodeIDs[k] = parentRedirection[skeleton.nodeIDs[k]];
				}
			}
		}

		if(node.parentNodeID != -1)
		{
			LogOptional("  Uses parent %d\r\n", node.parentNodeID);

			LogOptional("    Redirected to parent %d\r\n", parentRedirection[node.parentNodeID]);
		}

		if(node.effectID != -1)
		{
			LogOptional("  Uses effect %d\r\n", node.effectID);

			if(effectRedirection[node.effectID] == -1)
			{
				effectRedirection[node.effectID] = local.effects.size();
				local.effects.push_back(global.effects[node.effectID]);
				LogOptional("    Redirected to effect %d\r\n", effectRedirection[node.effectID]);
			}
		}

		nodeGeomList[i] = geomID;

		nodeInfo.controllerID = node.skeletonID == -1 ? -1 : skeletonRedirection[node.skeletonID];
		nodeInfo.parentID = node.parentNodeID == -1 ? -1 : parentRedirection[node.parentNodeID];
		nodeInfo.effectID = node.effectID == -1 ? -1 : effectRedirection[node.effectID];
		nodeInfo.flags = 0;

		LogOptional("  geomID: %d, parentID: %d, skeletonId: %d, geometry: %s\r\n", geomID, nodeInfo.parentID, nodeInfo.controllerID, stringData.data() + nodeInfo.geometryNameOffset);

		if((nodeID != -1 && parentRedirection[nodeID] == i))
		{
			LogOptional("  Adjusting object '%s' to the center of the scene\n", node.ID);
			LogOptional("  from %f %f %f to 0 0 0\n", node.model.mat[12], node.model.mat[13], node.model.mat[14]);

			nodeInfo.modelOriginal = node.model;
			nodeInfo.modelOriginal.mat[12] = 0.0f;
			nodeInfo.modelOriginal.mat[13] = 0.0f;
			nodeInfo.modelOriginal.mat[14] = 0.0f;
			nodeInfo.model = nodeInfo.modelOriginal;
		}
		else
		{
			nodeInfo.modelOriginal = node.model;
			nodeInfo.model = nodeInfo.modelOriginal;
		}

		fwrite(&nodeInfo, sizeof(nodeInfo), 1, fOut);

		nodeList[i] = nodeInfo;
	}

	// Save skeletons
	LogPrint("Saving controllers\r\n");
	unsigned controllerCount= (unsigned)local.skeletons.size();
	fwrite(&controllerCount, 4, 1, fOut);

	for(unsigned i = 0; i < local.skeletons.size(); i++)
	{
		auto&& skeleton = local.skeletons[i];

		LogOptional(" Skeleton %d has %d bones\r\n", i, skeleton.jointCount);

		Export::ControllerInfo sk;

		sk.boneCount = skeleton.jointCount;
		sk.bindPose = skeleton.bindShapeMat;

		fwrite(&sk, sizeof(sk), 1, fOut);

		// Save node IDs
		fwrite(&skeleton.nodeIDs[0], 4, skeleton.jointCount, fOut);

		// Save bind matrices
		fwrite(&skeleton.bindMat[0], 64, skeleton.jointCount, fOut);

		// Save bone geometry bounds
		fwrite(global.contrls[skeleton.controllerID]->bounds, sizeof(aabb), skeleton.jointCount, fOut);
	}

	// Find which nodes are animated in the local node tree
	std::vector<unsigned> animNodeRedirection;

	for(unsigned i = 0; i < global.animatedNodes.size(); i++)
	{
		unsigned id = global.animatedNodes[i];
		bool allowed = id == nodeID;

		for(unsigned k = 0; k < allowedRoots.size(); k++)
			allowed |= allowedRoots[k] == id;

		if(allowed)
		{
			animNodeRedirection.push_back(i);
			local.animatedNodes.push_back(parentRedirection[id]);

			LogPrint("  Animating node %d at index %d is the animating node %d at index %d in the old array\r\n", parentRedirection[id], local.animatedNodes.size() - 1, id, i);
		}
	}

	LogPrint("Out of %d animated nodes this tree contains %d\r\n", global.animatedNodes.size(), local.animatedNodes.size());

	local.animSampleCount = global.animSampleCount;

	if(local.animSampleCount == -1)
		local.animSampleCount = 0;

	// Find out animation matrices
	local.matrixAnimation = new mat4[local.animSampleCount * local.animatedNodes.size()];

	for(unsigned n = 0; n < local.animSampleCount; n++)
	{
		for(unsigned i = 0; i < local.animatedNodes.size(); i++)
		{
			auto &source = global.matrixAnimation[n * global.animatedNodes.size() + animNodeRedirection[i]];

			auto &target = local.matrixAnimation[n * local.animatedNodes.size() + i];

			target = source;

			auto &node = local.nodes[local.animatedNodes[i]];

			if(node.isSkeletonRoot)
			{
				target.mat[12] -= node.model.mat[12];
				target.mat[13] -= node.model.mat[13];
			}
		}
	}

	unsigned animNodeCount = local.animatedNodes.size();
	fwrite(&animNodeCount, 4, 1, fOut);

	if(local.animatedNodes.size())
		fwrite(&local.animatedNodes[0], 4, local.animatedNodes.size(), fOut);

	fwrite(&local.animSampleCount, 4, 1, fOut);

	if(local.animatedNodes.size() && local.animSampleCount)
		fwrite(local.matrixAnimation, sizeof(mat4), local.animSampleCount * local.animatedNodes.size(), fOut);

	unsigned frameCount = local.animSampleCount ? local.animSampleCount : 1;

	std::array<mat4, 256> bones;

	aabb *aabbState = new aabb[frameCount];

	for(unsigned n = 0; n < frameCount; n++)
	{
		// Prepare animation
		for(uint32_t i = 0; i < local.animatedNodes.size(); i++)
			nodeList[local.animatedNodes[i]].modelOriginal = local.matrixAnimation[local.animatedNodes.size() * n + i];

		// Prepare transformations
		for(unsigned i = 0; i < local.nodes.size(); i++)
		{
			Export::NodeInfo& node = nodeList[i];

			// Transform node by parent matrix
			if(node.parentID != -1)
				mul(node.model, nodeList[node.parentID].model, node.modelOriginal);
			else
				nodeList[i].model = node.modelOriginal;
		}

		bool aabbSet = false;
		for(unsigned i = 0; i < local.nodes.size(); i++)
		{
			Export::NodeInfo& node = nodeList[i];

			if(node.geometryNameOffset != 0) // Skip empty nodes
			{
				if(node.controllerID != -1)
				{
					DAESkeleton &skeleton = local.skeletons[node.controllerID];

					for(uint32_t i = 0; i < skeleton.jointCount; i++)
					{
						mul(bones[i], nodeList[skeleton.nodeIDs[i]].model, skeleton.bindMat[i]);

						aabb bounds = global.contrls[skeleton.controllerID]->bounds[i];
						bounds.mul(bones[i]);
						if(!aabbSet)
						{
							aabbState[n] = bounds;
							aabbSet = true;
						}
						else
						{
							aabbState[n].merge(bounds);
						}
					}
				}
				else
				{
					aabb bounds = global.geoms[nodeGeomList[i]]->bounds;
					bounds.mul(node.model);
					if(!aabbSet)
					{
						aabbState[n] = bounds;
						aabbSet = true;
					}
					else
					{
						aabbState[n].merge(bounds);
					}
				}
			}
		}
	}

	fwrite(aabbState, sizeof(aabb), frameCount, fOut);

	delete[] aabbState;

	unsigned materialCount = local.effects.size();
	fwrite(&materialCount, 4, 1, fOut);

	for(unsigned i = 0; i < local.effects.size(); i++)
	{
		auto&& effect = local.effects[i];

		Export::MaterialInfo info;

		if(effect.diffuseColor != ~0u)
			info.colorStringOffset = AppendString(stringData, global.images[effect.diffuseColor].path);
		else
			info.colorStringOffset = 0;

		if(effect.diffuseAlpha != ~0u)
			info.alphaStringOffset = AppendString(stringData, global.images[effect.diffuseAlpha].path);
		else
			info.alphaStringOffset = 0;

		fwrite(&info, sizeof(info), 1, fOut);
	}

	unsigned stringSize = stringData.size();

	fwrite(&stringSize, 4, 1, fOut);
	fwrite(stringData.data(), 1, stringData.size(), fOut);

	fclose(fOut);

	LogPrint("-------------------------------\r\n");
}
