#include <stdio.h>
#include <memory.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include <cassert>

#include <algorithm>
#include <unordered_map>

#include "../pugixml/src/pugixml.hpp"

#include "context.h"
#include "export.h"

Context global;

const char* fastatoui(const char* str, unsigned& v)
{
	unsigned digit;
	unsigned a = 0;
	while((digit = *str - '0') < 10)
	{
		a = a * 10 + digit;
		str++;
	}
	v = a;
	return str;
}

const char* fastatoi(const char* str, int& v)
{
	unsigned digit;
	int sgn = 1;
	if(str[0] == '-')
	{
		sgn = -1;
		str++;
	}
	int a = 0;
	while((digit = *str - '0') < 10)
	{
		a = a * 10 + digit;
		str++;
	}
	v = a*sgn;
	return str;
}
const char* fastatof(const char* str, float& ft)
{
	unsigned digit;
	unsigned Left = 0, Right = 0;
	double sign = 1.0;
	if(str[0] == 'N' && str[1] == 'a' && str[2] == 'N')
	{
		ft = 0;
		return str + 3;
	}
	if(str[0] == '-')
	{
		sign = -1.0;
		str++;
	}
	while((digit = *str - '0') < 10)
	{
		Left = Left * 10 + digit;
		str++;
	}

	double mul = 1.0;
	if(str[0] == '.')
	{
		str++;
		while((digit = *str - '0') < 10)
		{
			Right = Right * 10 + digit;
			mul *= 0.1;
			str++;
		}
	}
	if(str[0] == 'e' || str[0] == 'E')
	{
		str++;
		bool negative = *str == '-';
		unsigned e;
		str = fastatoui(str + negative, e);
		if(negative)
			ft = float(sign * (Left + Right * mul) * pow(10.0, -(double)e));
		else
			ft = float(sign * (Left + Right * mul) * pow(10.0, (double)e));
		return str;
	}
	ft = float((Left + Right * mul) * sign);
	return str;
}

FILE *logFile = NULL;

unsigned streamCount = 0;
char *data = NULL;
int fsize = 0;

pugi::xml_document doc;

void LogPrint(const char* format, ...)
{
	va_list args;
#ifdef _DEBUG
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
#endif

	va_start(args, format);
	vfprintf(logFile, format, args);
	fflush(logFile);
	va_end(args);
}

#ifdef LOG_VERBOSE
#define LogOptional LogPrint
#else
#define LogOptional(...) (void)0
#endif

bool LoadFile(char* fileNameIn)
{
	FILE *fIN = fopen(fileNameIn, "rb");
	if(!fIN)
	{
		LogPrint("File not found\r\n");
		return false;
	}
	fsize = -1;
	fseek(fIN, 0, SEEK_END);
	fsize = ftell(fIN);
	fseek(fIN, 0, SEEK_SET);
	LogPrint("Size of file is %d bytes\r\n", fsize);

	data = new char[fsize + 1];
	data[fsize] = 0;
	fread(data, 1, fsize, fIN);
	fclose(fIN);

	LogPrint("Load File done\r\n");
	return true;
}

void ParseFile(char* data)
{
	LogPrint("Going to parse\r\n");
	doc.load(data);
}

void LoadMaterialLibrary()
{
	LogPrint("Parsing images\r\n");

	if(pugi::xml_node libImages = doc.child("COLLADA").child("library_images"))
	{
		for(pugi::xml_node img = libImages.child("image"); img; img = img.next_sibling("image"))
		{
			DAEImage image;

			image.ID = img.attribute("id").value();
			image.name = img.attribute("name").value();

			image.path = img.child("init_from").child_value();

			auto fwd = strrchr(image.path, '/');
			auto bkw = strrchr(image.path, '\\');

			if(fwd && bkw)
				image.path = (fwd > bkw ? fwd : bkw) + 1;
			else if(fwd)
				image.path = fwd + 1;
			else if(bkw)
				image.path = bkw + 1;

			global.images.push_back(image);
		}
	}

	LogPrint("Parsing effects\r\n");

	if(pugi::xml_node libImages = doc.child("COLLADA").child("library_effects"))
	{
		for(pugi::xml_node fx = libImages.child("effect"); fx; fx = fx.next_sibling("effect"))
		{
			DAEEffect effect;

			effect.ID = fx.attribute("id").value();
			effect.name = fx.attribute("name").value();

			auto diffuseColor = fx.child("profile_COMMON").child("technique").child("phong").child("diffuse").child("texture").attribute("texture").value();
			auto diffuseAlpha = fx.child("profile_COMMON").child("technique").child("phong").child("transparent").child("texture").attribute("texture").value();

			effect.diffuseColor = ~0u;

			for(unsigned i = 0; i < global.images.size(); i++)
			{
				if(strcmp(global.images[i].ID, diffuseColor) == 0)
					effect.diffuseColor = i;
			}

			effect.diffuseAlpha = ~0u;

			for(unsigned i = 0; i < global.images.size(); i++)
			{
				if(strcmp(global.images[i].ID, diffuseAlpha) == 0)
					effect.diffuseAlpha = i;
			}

			global.effects.push_back(effect);
		}
	}

	LogPrint("Parsing material\r\n");

	if(pugi::xml_node libImages = doc.child("COLLADA").child("library_materials"))
	{
		for(pugi::xml_node mat = libImages.child("material"); mat; mat = mat.next_sibling("material"))
		{
			DAEMaterial material;

			material.ID = mat.attribute("id").value();
			material.name = mat.attribute("name").value();

			auto effect = mat.child("instance_effect").attribute("url").value();

			material.effectID = ~0u;

			for(unsigned i = 0; i < global.effects.size(); i++)
			{
				if(*effect && strcmp(global.effects[i].ID, effect + 1) == 0)
					material.effectID = i;
			}

			global.materials.push_back(material);
		}
	}
}

void LoadGeometryLibrary()
{
	global.geoms.clear();
	LogPrint("Parsing geometries\r\n");

	pugi::xml_node library = doc.child("COLLADA").child("library_geometries");
	assert(library);

	for(pugi::xml_node geom = library.child("geometry"); geom; geom = geom.next_sibling("geometry"))
	{
		streamCount = 0;
		if(!geom.child("mesh"))
			continue;
		LogOptional("geometry. ID: %s, Name: %s\r\n", geom.attribute("id").value(), geom.attribute("name").value());
		global.geoms.push_back(new DAEGeometry());
		DAEGeometry &geometry = *global.geoms.back();
		geometry.ID = geom.attribute("id").value();
		geometry.name = geom.attribute("name").value();

		for(pugi::xml_node source = geom.child("mesh").child("source"); source; source = source.next_sibling("source"))
		{
			float *targetArr = NULL;

			geometry.streams[streamCount].name = source.attribute("id").value();
			geometry.streams[streamCount].count = source.child("float_array").attribute("count").as_int();

			pugi::xml_node accessor = source.child("technique_common").child("accessor");
			assert(accessor);
			geometry.streams[streamCount].stride = accessor.attribute("stride").as_int();
			targetArr = geometry.streams[streamCount].data = new float[geometry.streams[streamCount].count];

			LogOptional("\tsource. ID: %s, count: %s, stride: %s\r\n", source.attribute("id").value(), source.child("float_array").attribute("count").value(), accessor.attribute("stride").value());

			const char *rawArr = source.child_value("float_array");
			for(int i = 0, e = geometry.streams[streamCount].count; i != e; i++)
			{
				while((unsigned)*rawArr <= ' ')
					rawArr++;
				rawArr = fastatof(rawArr, targetArr[i]);
			}

			streamCount++;
		}

		for(pugi::xml_node input = geom.child("mesh").child("vertices").child("input"); input; input = input.next_sibling("input"))
		{
			auto inputSemantic = input.attribute("semantic").value();
			auto inputSource = input.attribute("source").value();

			// Find stream by name
			unsigned streamID = -1;
			for(unsigned i = 0; i < streamCount; i++)
			{
				if(strcmp(inputSource + 1, geometry.streams[i].name) == 0)
				{
					streamID = i;
					break;
				}
			}
			assert(streamID != -1 && "Stream referenced in <vertices> input couldn't be found");
			geometry.streams[streamID].indexOffset = -1;	// This will change later in <triangles> parse section, where the offset will become known

			// Search for known semantics
			for(int i = 0; i < ST_COUNT; i++)
			{
				if(strcmp(inputSemantic, semanticName[i]) == 0)
					geometry.streamLink[i] = streamID;
			}
		}

		pugi::xml_node triangles = geom.child("mesh").child("triangles");

		if(!triangles)
			triangles = geom.child("mesh").child("polylist");

		assert(triangles);

		unsigned inputsCount = 0;
		for(pugi::xml_node input = triangles.child("input"); input; input = input.next_sibling("input"))
		{
			auto inputSemantic = input.attribute("semantic").value();
			auto inputSource = input.attribute("source").value();
			auto inputOffset = input.attribute("offset").as_uint();

			if(inputOffset >= inputsCount)
				inputsCount = inputOffset + 1;

			// Search for main semantic
			if(strcmp(inputSemantic, "VERTEX") == 0)
			{
				for(unsigned i = 0; i < streamCount; i++)
				{
					if(geometry.streams[i].indexOffset == -1)
						geometry.streams[i].indexOffset = inputOffset;
				}
			}
			// Search for known semantics
			for(int i = 0; i < ST_COUNT; i++)
			{
				if(strcmp(inputSemantic, semanticName[i]) == 0)
				{
					// Find stream by name
					unsigned streamID = ~0u;
					for(unsigned n = 0; n < streamCount; n++)
					{
						if(strcmp(inputSource + 1, geometry.streams[n].name) == 0)
						{
							streamID = n;
							break;
						}
					}
					assert(streamID != -1 && "Stream referenced in <triangles> input couldn't be found");
					geometry.streamLink[i] = streamID;

					geometry.streams[streamID].indexOffset = inputOffset;
				}
			}
		}

		pugi::xpath_node_set nodeSet = geom.child("mesh").select_nodes("./triangles | ./polylist");
		nodeSet.sort();

		// Collect total count
		unsigned totalCount = 0;

		for(pugi::xpath_node_set::const_iterator node = nodeSet.begin(); node != nodeSet.end(); node++)
		{
			totalCount += node->node().attribute("count").as_int() * 3;
		}

		geometry.indCount = totalCount;
		geometry.indices = new IndexGroup[geometry.indCount];
		memset(geometry.indices, 0, geometry.indCount * sizeof(IndexGroup));
		IndexGroup *targetArr = geometry.indices;

		assert(inputsCount <= sizeof(geometry.indices[0].indData) / sizeof(geometry.indices[0].indData[0]));

		unsigned lastPos = 0;

		for(pugi::xpath_node_set::const_iterator node = nodeSet.begin(); node != nodeSet.end(); node++)
		{
			const char *rawArr = node->node().child_value("p");

			while(*rawArr)
			{
				for(unsigned n = 0; n < inputsCount; n++)
				{
					while((unsigned)*rawArr <= ' ')
						rawArr++;

					assert(lastPos < geometry.indCount);
					rawArr = fastatoui(rawArr, targetArr[lastPos].indData[n]);
				}

				lastPos++;
			}
		}

		assert(lastPos == geometry.indCount && "Coulndn't parse all <triangles> data");
	}
}

namespace std
{
	template<> struct hash<IndexGroup>
	{
		unsigned operator()(const IndexGroup& x) const
		{
			unsigned hash = 2166136261;
			for(int i = 0; i < 5; i++)
			{
				hash *= 16777619;
				hash ^= x.indData[i];
			}
			return hash;
		}
	};

	template<> struct hash<vec2>
	{
		unsigned operator()(const vec2& x) const
		{
			unsigned char *data = (unsigned char*)&x.x;

			unsigned hash = 5318;

			for(int i = 0; i < sizeof(vec2); i++)
				hash = ((hash << 5) + hash) + data[i];

			return hash;
		}
	};

	template<> struct hash<vec3>
	{
		unsigned operator()(const vec3& x) const
		{
			unsigned char *data = (unsigned char*)&x.x;

			unsigned hash = 5318;

			for(int i = 0; i < sizeof(vec3); i++)
				hash = ((hash << 5) + hash) + data[i];

			return hash;
		}
	};
}

void CreateIBVB()
{
	std::unordered_map<IndexGroup, unsigned> myMap;

	std::unordered_map<vec3, unsigned> posMap;
	std::unordered_map<vec2, unsigned> uvMap;
	std::unordered_map<vec3, unsigned> normalMap;

	vec3 currPos;
	vec2 currUV;
	vec3 currNormal;

	vec3 dummy(0, 0, 0);

	for(unsigned n = 0; n < global.geoms.size(); n++)
	{
		DAEGeometry *g = global.geoms[n];

		g->VB.resize(g->streams[g->streamLink[ST_POSITION]].count);
		g->VB.clear();

		g->IB.resize(g->indCount);
		g->IB.clear();

		unsigned lastIndex = 0;

		myMap.clear();

		posMap.clear();
		uvMap.clear();
		normalMap.clear();

		unsigned indices[] = {
			g->streams[g->streamLink[ST_POSITION] == ~0u ? 0 : g->streamLink[ST_POSITION]].indexOffset,
			g->streams[g->streamLink[ST_TEXCOORD] == ~0u ? 0 : g->streamLink[ST_TEXCOORD]].indexOffset,
			g->streams[g->streamLink[ST_NORMAL] == ~0u ? 0 : g->streamLink[ST_NORMAL]].indexOffset,
			g->streams[g->streamLink[ST_BINORMAL] == ~0u ? 0 : g->streamLink[ST_BINORMAL]].indexOffset,
			g->streams[g->streamLink[ST_TANGENT] == ~0u ? 0 : g->streamLink[ST_TANGENT]].indexOffset };

		unsigned strides[] = {
			g->streamLink[ST_POSITION] == ~0u ? 0 : g->streams[g->streamLink[ST_POSITION]].stride,
			g->streamLink[ST_TEXCOORD] == ~0u ? 0 : g->streams[g->streamLink[ST_TEXCOORD]].stride,
			g->streamLink[ST_NORMAL] == ~0u ? 0 : g->streams[g->streamLink[ST_NORMAL]].stride,
			g->streamLink[ST_BINORMAL] == ~0u ? 0 : g->streams[g->streamLink[ST_BINORMAL]].stride,
			g->streamLink[ST_TANGENT] == ~0u ? 0 : g->streams[g->streamLink[ST_TANGENT]].stride };

		float *dataArrs[] = {
			g->streamLink[ST_POSITION] == ~0u ? &dummy.x : g->streams[g->streamLink[ST_POSITION]].data,
			g->streamLink[ST_TEXCOORD] == ~0u ? &dummy.x : g->streams[g->streamLink[ST_TEXCOORD]].data,
			g->streamLink[ST_NORMAL] == ~0u ? &dummy.x : g->streams[g->streamLink[ST_NORMAL]].data,
			g->streamLink[ST_BINORMAL] == ~0u ? &dummy.x : g->streams[g->streamLink[ST_BINORMAL]].data,
			g->streamLink[ST_TANGENT] == ~0u ? &dummy.x : g->streams[g->streamLink[ST_TANGENT]].data, };

		unsigned remapPos = 0;
		unsigned remapUV = 0;
		unsigned remapNormal = 0;

		unsigned remapVertex = 0;

		for(unsigned i = 0; i < g->indCount; i++)
		{
			auto oldMit = myMap.find(g->indices[i]);

			unsigned *indData = g->indices[i].indData;

			memcpy(&currPos, dataArrs[0] + indData[indices[0]] * strides[0], sizeof(vec3));
			memcpy(&currUV, dataArrs[1] + indData[indices[1]] * strides[1], sizeof(vec2));
			memcpy(&currNormal, dataArrs[2] + indData[indices[2]] * strides[2], sizeof(vec3));

			if(g->streamLink[ST_POSITION] != ~0u)
			{
				auto it = posMap.find(currPos);

				if(it != posMap.end())
				{
					if(indData[indices[0]] != it->second)
					{
						remapPos++;
						indData[indices[0]] = it->second;
					}
				}
				else
				{
					posMap[currPos] = indData[indices[0]];
				}
			}

			if(g->streamLink[ST_TEXCOORD] != ~0u)
			{
				auto it = uvMap.find(currUV);

				if(it != uvMap.end())
				{
					if(indData[indices[1]] != it->second)
					{
						remapUV++;
						indData[indices[1]] = it->second;
					}
				}
				else
				{
					uvMap[currUV] = indData[indices[1]];
				}
			}

			if(g->streamLink[ST_NORMAL] != ~0u)
			{
				auto it = normalMap.find(currNormal);

				if(it != normalMap.end())
				{
					if(indData[indices[2]] != it->second)
					{
						remapNormal++;
						indData[indices[2]] = it->second;
					}
				}
				else
				{
					normalMap[currNormal] = indData[indices[2]];
				}
			}

			auto mit = myMap.find(g->indices[i]);

			if(mit != oldMit)
				remapVertex++;

			if(mit != myMap.end())
			{
				g->IB.push_back(mit->second);
			}
			else
			{
				g->IB.push_back(lastIndex);
				myMap[g->indices[i]] = lastIndex++;

				g->VB.push_back(Vertex());
				Vertex &v = g->VB.back();

				g->posIndex.push_back(g->indices[i].indData[indices[0]]);

				memcpy(&v.pos, dataArrs[0] + indData[indices[0]] * strides[0], 4 * 3);

				v.tc[0] = short(*(dataArrs[1] + indData[indices[1]] * strides[1]) * 32767);
				v.tc[1] = short((1.0f - *(dataArrs[1] + indData[indices[1]] * strides[1] + 1)) * 32767);

				v.normal[0] = short(*(dataArrs[2] + indData[indices[2]] * strides[2]) * 32767);
				v.normal[1] = short(*(dataArrs[2] + indData[indices[2]] * strides[2] + 1) * 32767);
				v.normal[2] = short(*(dataArrs[2] + indData[indices[2]] * strides[2] + 2) * 32767);

#if defined(EXPORT_BINORMALS)
				if(g->streamLink[ST_TANGENT] != 0)
				{
					v.binormal[0] = short(*(dataArrs[3] + indData[indices[3]] * strides[3]) * 32767);
					v.binormal[1] = short(*(dataArrs[3] + indData[indices[3]] * strides[3] + 1) * 32767);
					v.binormal[2] = short(*(dataArrs[3] + indData[indices[3]] * strides[3] + 2) * 32767);

					v.tangent[0] = short(*(dataArrs[4] + indData[indices[4]] * strides[4]) * 32767);
					v.tangent[1] = short(*(dataArrs[4] + indData[indices[4]] * strides[4] + 1) * 32767);
					v.tangent[2] = short(*(dataArrs[4] + indData[indices[4]] * strides[4] + 2) * 32767);
				}
#endif
			}
		}

		vec3 boundsMin = vec3(1e12f, 1e12f, 1e12f);
		vec3 boundMax = -boundsMin;

		for(unsigned i = 0; i < g->VB.size(); i++)
		{
			boundsMin.x = boundsMin.x < g->VB[i].pos[0] ? boundsMin.x : g->VB[i].pos[0];
			boundsMin.y = boundsMin.y < g->VB[i].pos[1] ? boundsMin.y : g->VB[i].pos[1];
			boundsMin.z = boundsMin.z < g->VB[i].pos[2] ? boundsMin.z : g->VB[i].pos[2];

			boundMax.x = boundMax.x > g->VB[i].pos[0] ? boundMax.x : g->VB[i].pos[0];
			boundMax.y = boundMax.y > g->VB[i].pos[1] ? boundMax.y : g->VB[i].pos[1];
			boundMax.z = boundMax.z > g->VB[i].pos[2] ? boundMax.z : g->VB[i].pos[2];
		}

		g->bounds.center = (boundsMin + boundMax) / 2.0;
		g->bounds.size = (boundMax - boundsMin) / 2.0;

		LogOptional("Geom %d Vertices %d Triangles %d\r\n", n, g->VB.size(), g->IB.size() / 3);
		LogOptional("Geom %d Remapping %d/%d/%d for %d\r\n", n, remapPos, remapUV, remapNormal, remapVertex);
	}
}

DAESource ParseSource(pugi::xml_node source, bool specialCaseNameArray = false)
{
	DAESource src;
	memset(&src, 0, sizeof(DAESource));

	src.ID = source.attribute("id").value();
	src.count = source.child("technique_common").child("accessor").attribute("count").as_int();
	src.stride = source.child("technique_common").child("accessor").attribute("stride").as_int(1);

	if(source.child("Name_array"))
	{
		src.dataNameBlob = strdup(source.child_value("Name_array"));
		src.dataName = new char*[src.count * src.stride];

		char *rawArr = src.dataNameBlob;
		char **target = src.dataName;

		if(specialCaseNameArray)
		{
			pugi::xpath_node_set nodeSet = doc.child("COLLADA").child("library_visual_scenes").select_nodes("//node");

			auto remainingLength = strlen(rawArr);

			for(unsigned i = 0; i < src.count * src.stride; i++)
			{
				while((unsigned)*rawArr <= ' ')
				{
					rawArr++;
					remainingLength--;
				}

				unsigned largestMatchSize = 0;

				for(pugi::xpath_node_set::const_iterator node = nodeSet.begin(); node != nodeSet.end(); node++)
				{
					auto name = node->node().attribute("name").value();
					auto nameLength = (unsigned)strlen(name);

					if(nameLength <= remainingLength && nameLength > largestMatchSize && memcmp(rawArr, name, nameLength) == 0)
						largestMatchSize = nameLength;
				}

				assert(largestMatchSize != 0);

				target[i] = rawArr;

				rawArr += largestMatchSize;
				remainingLength -= largestMatchSize;

				*rawArr = 0;
			}
		}
		else
		{
			for(unsigned i = 0; i < src.count * src.stride; i++)
			{
				while((unsigned)*rawArr <= ' ')
					rawArr++;

				target[i] = rawArr;

				while((unsigned)*rawArr > ' ')
					rawArr++;

				*rawArr = 0;
			}
		}
	}
	else if(source.child("float_array"))
	{
		src.dataFloat = new float[src.count * src.stride];

		const char *rawArr = source.child_value("float_array");
		float *target = src.dataFloat;

		for(unsigned i = 0; i < src.count * src.stride; i++)
		{
			while((unsigned)*rawArr <= ' ')
				rawArr++;

			rawArr = fastatof(rawArr, target[i]);
		}
	}
	else
	{
		assert(!"unknown source data type");
	}
	return src;
}

DAESource* FindSource(const char* ID, DAESource* sources, unsigned count)
{
	if(!ID)
		return NULL;

	for(unsigned i = 0; i < count; i++)
	{
		if(strcmp(ID + 1, sources[i].ID) == 0)
			return &sources[i];
	}

	return NULL;
}

std::vector<DAESource> animSource;
std::vector<DAEAnimation*> anims;

void LoadAnimationLibrary()
{
	anims.clear();

	LogPrint("Parsing animations\r\n");

	pugi::xml_node library = doc.child("COLLADA").child("library_animations");

	if(!library)
		return;

	// Get all sources for parsing
	animSource.clear();

	pugi::xpath_node_set nodeSet = library.select_nodes("animation//source");

	for(pugi::xpath_node_set::const_iterator node = nodeSet.begin(); node != nodeSet.end(); node++)
		animSource.push_back(ParseSource(node->node()));

	nodeSet = library.select_nodes("animation//channel");

	for(pugi::xpath_node_set::const_iterator node = nodeSet.begin(); node != nodeSet.end(); node++)
	{
		const pugi::xml_node animation = node->node();

		LogOptional("Loading animation \"%s\"\r\n", animation.attribute("id").value());

		anims.push_back(new DAEAnimation());
		DAEAnimation &last = *anims.back();
		memset(&last, 0, sizeof(DAEAnimation));

		last.ID = animation.attribute("id").value();

		last.targetNode = strdup(animation.attribute("target").value());
		char *dividerPos = strchr(last.targetNode, '/');

		if(dividerPos)
		{
			last.targetSID = dividerPos + 1;
			*dividerPos = 0;
			char *pointPos = strchr(last.targetSID, '.') ? strchr(last.targetSID, '.') : strchr(last.targetSID, '(');

			if(pointPos)
			{
				last.targetComponent = pointPos + 1;
				*pointPos = 0;

				if(strcmp(last.targetComponent, "X") == 0)
					last.compOffset = 0;

				if(strcmp(last.targetComponent, "Y") == 0)
					last.compOffset = 1;

				if(strcmp(last.targetComponent, "Z") == 0)
					last.compOffset = 2;

				if(strcmp(last.targetComponent, "ANGLE") == 0)
					last.compOffset = 3;

				if((unsigned)(*last.targetComponent - '0') < 10)
				{
					unsigned a = 0;
					fastatoui(last.targetComponent, a);

					if(char *par2 = strchr(last.targetComponent, '('))
					{
						unsigned b = 0;
						fastatoui(par2 + 1, b);
						last.compOffset = a + 4 * b;
					}
					else
					{
						last.compOffset = a;
					}
				}
			}
		}

		pugi::xpath_variable_set vars;
		vars.set("samp_name", animation.attribute("source").value() + 1);

		assert(strcmp("animation", animation.parent().name()) == 0);
		pugi::xml_node parent = animation.parent();

		last.inSource = FindSource(parent.select_single_node("sampler[@id = $samp_name]/input[@semantic='INPUT']/@source", &vars).attribute().value(), animSource.data(), animSource.size());
		assert(last.inSource);
		last.outSource = FindSource(parent.select_single_node("sampler[@id = $samp_name]/input[@semantic='OUTPUT']/@source", &vars).attribute().value(), animSource.data(), animSource.size());
		assert(last.outSource);
		last.inTangentSource = FindSource(parent.select_single_node("sampler[@id = $samp_name]/input[@semantic='IN_TANGENT']/@source", &vars).attribute().value(), animSource.data(), animSource.size());
		last.outTangentSource = FindSource(parent.select_single_node("sampler[@id = $samp_name]/input[@semantic='OUT_TANGENT']/@source", &vars).attribute().value(), animSource.data(), animSource.size());
		last.interpolationStr = FindSource(parent.select_single_node("sampler[@id = $samp_name]/input[@semantic='INTERPOLATION']/@source", &vars).attribute().value(), animSource.data(), animSource.size());

		last.dataCount = last.inSource->count;

		assert(last.interpolationStr);
		last.interpolation = new Interpolation[last.interpolationStr->count * last.interpolationStr->stride];

		for(unsigned i = 0; i < last.interpolationStr->count * last.interpolationStr->stride; i++)
		{
			char *curr = last.interpolationStr->dataName[i];

			last.interpolation[i] = I_UNSUPPORTED;

			if(memcmp(curr, "LINEAR", 6) == 0)
				last.interpolation[i] = LINEAR;
			else if(memcmp(curr, "BEZIER", 6) == 0)
				last.interpolation[i] = BEZIER;
			else if(memcmp(curr, "STEP", 4) == 0)
				last.interpolation[i] = STEP;

			assert(last.interpolation[i] != I_UNSUPPORTED && "unsupported interpolation method");
		}
	}
}

struct WeightBone
{
	WeightBone(): weight(0.0f), bone(0)
	{
	}

	float weight;
	int bone;
};

void LoadControllerLibrary()
{
	global.contrls.clear();

	LogPrint("Parsing controllers\r\n");

	pugi::xml_node library = doc.child("COLLADA").child("library_controllers");

	if(!library)
		return;

	for(pugi::xml_node controller = library.child("controller"); controller; controller = controller.next_sibling("controller"))
	{
		LogOptional("id: %s\r\n", controller.attribute("id").value());

		global.contrls.push_back(new DAEController());
		DAEController &curr = *global.contrls.back();
		memset(&curr, 0, sizeof(DAEController));

		curr.ID = controller.attribute("id").value();

		// Find skin node
		pugi::xml_node skin = controller.child("skin");
		assert(skin);

		LogOptional("source: %s\r\n", skin.attribute("source").value());

		curr.source = skin.attribute("source").value(); // save source geometry name

		// Find corresponding geometry index
		const char *targetName = curr.source + 1;
		curr.geometryID = -1;

		for(unsigned i = 0; i < global.geoms.size(); i++)
		{
			if(strcmp(global.geoms[i]->ID, targetName) == 0)
			{
				curr.geometryID = i;
				break;
			}
		}

		LogOptional("geometryID: %d\r\n", curr.geometryID);

		if(curr.geometryID == -1)
		{
			LogOptional("Corresponding geometry not found!\r\n");
			return;
		}

		// Find bind matrix
		pugi::xml_node bind_shape = skin.child("bind_shape_matrix");
		assert(bind_shape);

		const char *rawArr = bind_shape.child_value();

		for(unsigned n = 0; n < 16; n++)
		{
			while((unsigned)*rawArr <= ' ')
				rawArr++;

			rawArr = fastatof(rawArr, curr.bindMat.mat[n]);
		}

		curr.bindMat = curr.bindMat.transpose();

		// Find joints, bind poses and weights
		for(pugi::xml_node source = skin.child("source"); source; source = source.next_sibling("source"))
		{
			assert(curr.sourceCount < 8);
			curr.sources[curr.sourceCount++] = ParseSource(source, strstr(source.attribute("id").value(), "-Joints") != NULL);
		}

		curr.joints = FindSource(skin.select_single_node("joints/input[@semantic='JOINT']/@source").attribute().value(), curr.sources.data(), curr.sourceCount);
		curr.binds = FindSource(skin.select_single_node("joints/input[@semantic='INV_BIND_MATRIX']/@source").attribute().value(), curr.sources.data(), curr.sourceCount);
		assert(curr.joints == FindSource(skin.select_single_node("vertex_weights/input[@semantic='JOINT']/@source").attribute().value(), curr.sources.data(), curr.sourceCount));
		curr.weights = FindSource(skin.select_single_node("vertex_weights/input[@semantic='WEIGHT']/@source").attribute().value(), curr.sources.data(), curr.sourceCount);

		assert(curr.binds && curr.binds->dataFloat && curr.binds->stride == 16);
		assert(curr.joints && curr.joints->dataName && curr.joints->stride == 1);
		assert(curr.weights && curr.weights->dataFloat && curr.weights->stride == 1);

		pugi::xml_node weights = skin.child("vertex_weights");
		assert(weights);

		curr.vcountCount = weights.attribute("count").as_int();
		curr.vcountData = new unsigned[curr.vcountCount];

		LogOptional("\tvcount count: %d\r\n", curr.vcountCount);

		unsigned vCount = 0;
		const char *start = weights.child_value("vcount");

		for(unsigned i = 0; i < curr.vcountCount; i++)
		{
			while((unsigned)*start <= ' ')
				start++;

			start = fastatoui(start, curr.vcountData[i]);
			vCount += curr.vcountData[i];
		}

		curr.vCount = vCount * 2;
		curr.vData = new int[curr.vCount];

		LogOptional("\tv count: %d\r\n", curr.vCount);

		start = weights.child_value("v");

		for(unsigned i = 0; i < curr.vCount; i++)
		{
			while((unsigned)*start <= ' ')
				start++;

			start = fastatoi(start, curr.vData[i]);

			assert(curr.vData[i] >= 0);
		}
	}

	LogPrint("Fixup controllers\r\n");

	std::array<WeightBone, 32> tmpWaI;	// Weight and bone index

	for(unsigned i = 0; i < global.contrls.size(); i++)
	{
		DAEController &curr = *global.contrls[i];

		curr.exIndices = new unsigned char[curr.vcountCount * 4];
		curr.exWeights = new short[curr.vcountCount * 4];
		curr.bounds = new aabb[curr.joints->count];

		bool *accessed = new bool[curr.joints->count];
		memset(accessed, 0, sizeof(bool) * curr.joints->count);

		for(unsigned n = 0; n < curr.joints->count; n++)
		{
			curr.bounds[n].center = vec3(1e12f, 1e12f, 1e12f);
			curr.bounds[n].size = -vec3(1e12f, 1e12f, 1e12f);
		}

		DAEGeometry *g = global.geoms[curr.geometryID];
		StreamInfo *vInfo = &g->streams[g->streamLink[ST_POSITION]];

		unsigned ind = 0;
		for(unsigned n = 0; n < curr.vcountCount; n++)
		{
			// Load all weights and bone indices
			unsigned indSaved = ind;
			for(unsigned p = 0; p < curr.vcountData[n]; p++)
			{
				tmpWaI[p].bone = curr.vData[ind];
				ind++;
				tmpWaI[p].weight = curr.weights->dataFloat[curr.vData[ind]];
				ind++;
			}

			// Find 4 main elements
			for(unsigned k = 0; k < curr.vcountData[n]; k++)
			{
				for(unsigned l = k + 1; l < curr.vcountData[n]; l++)
				{
					if(tmpWaI[k].weight < tmpWaI[l].weight)
					{
						WeightBone tmp = tmpWaI[k];
						tmpWaI[k] = tmpWaI[l];
						tmpWaI[l] = tmp;
					}
				}
			}

			tmpWaI[3].weight = 0.0f;

			// Normalize weights
			float sum = tmpWaI[0].weight + tmpWaI[1].weight + tmpWaI[2].weight + tmpWaI[3].weight;

			tmpWaI[0].weight /= sum;
			tmpWaI[1].weight /= sum;
			tmpWaI[2].weight /= sum;
			tmpWaI[3].weight /= sum;

			// Save data
			curr.exIndices[n * 4 + 0] = tmpWaI[0].bone;
			curr.exIndices[n * 4 + 1] = tmpWaI[1].bone;
			curr.exIndices[n * 4 + 2] = tmpWaI[2].bone;
			curr.exIndices[n * 4 + 3] = tmpWaI[3].bone;

			curr.exWeights[n * 4 + 0] = short(tmpWaI[0].weight * 32767);
			curr.exWeights[n * 4 + 1] = short(tmpWaI[1].weight * 32767);
			curr.exWeights[n * 4 + 2] = short(tmpWaI[2].weight * 32767);
			curr.exWeights[n * 4 + 3] = short(tmpWaI[3].weight * 32767);

			// center used as minimum
			// sized used as maximum
			for(unsigned k = 0; k < curr.vcountData[n]; k++)
			{
				accessed[curr.vData[indSaved + k * 2]] = true;

				vec3 &min = curr.bounds[curr.vData[indSaved + k * 2]].center;
				vec3 &max = curr.bounds[curr.vData[indSaved + k * 2]].size;

				float *vertex = vInfo->data + n * vInfo->stride;
				min.x = min.x < vertex[0] ? min.x : vertex[0];
				min.y = min.y < vertex[1] ? min.y : vertex[1];
				min.z = min.z < vertex[2] ? min.z : vertex[2];

				max.x = max.x > vertex[0] ? max.x : vertex[0];
				max.y = max.y > vertex[1] ? max.y : vertex[1];
				max.z = max.z > vertex[2] ? max.z : vertex[2];
			}

			for(unsigned i = 0; i < curr.vcountData[n]; i++)
			{
				tmpWaI[i].bone = 0;
				tmpWaI[i].weight = 0.0;
			}
		}

		for(unsigned n = 0; n < curr.joints->count; n++)
		{
			if(!accessed[n])
				curr.bounds[n].center = curr.bounds[n].size = vec3(0, 0, 0);

			vec3 min = curr.bounds[n].center;
			vec3 max = curr.bounds[n].size;

			curr.bounds[n].center = (min + max) / 2.0;
			curr.bounds[n].size = (max - min) / 2.0;
		}

		delete[] accessed;
	}
}

unsigned NodeDepth(pugi::xml_node n)
{
	unsigned d = 0;

	while(n)
	{
		d++;
		n = n.parent();
	}

	return d;
}

bool LoadScene()
{
	std::array<unsigned, 128> stack;

	unsigned top = 0;
	global.nodes.clear();
	global.skeletons.clear();
	global.matrixAnimation = NULL;

	pugi::xml_node scene = doc.child("COLLADA").child("library_visual_scenes").child("visual_scene");

	if(!scene)
		return false;

	LogPrint("Parse scene info\r\n");

	DAENode newNode;
	DAESkeleton newSkeleton;
	mat4 tempTransform;

	unsigned sDepth = NodeDepth(scene);
	pugi::xpath_node_set nodeSet = scene.select_nodes("//node");
	nodeSet.sort();

	for(pugi::xpath_node_set::const_iterator node = nodeSet.begin(); node != nodeSet.end(); node++)
	{
		const pugi::xml_node n = node->node();

		unsigned depth = NodeDepth(n);
		assert(depth < 128);
		stack[depth - sDepth] = unsigned(global.nodes.size());

		// Save name
		newNode.ID = n.attribute("id").value();
		newNode.name = n.attribute("name").value();
		newNode.sid = n.attribute("sid").value();	// Not necessarily present, but (NULL, NULL) string will be fine

		// Is it a joint?
		newNode.isJoint = strcmp("JOINT", n.attribute("type").value()) == 0;

		// Find transformation matrix
		newNode.model.identity();
		newNode.tCount = 0;

		pugi::xpath_node_set transforms = n.select_nodes("./matrix | ./rotate | ./translate | ./scale | ./lookat | ./skew");
		transforms.sort();

		for(pugi::xpath_node_set::const_iterator transform = transforms.begin(); transform != transforms.end(); transform++)
		{
			const pugi::xml_node t = transform->node();
			tempTransform.identity();

			if(strcmp(t.name(), "matrix") == 0)
			{
				const char* str = t.child_value();

				for(int n = 0; n < 16; n++)
					str = fastatof(str, tempTransform.mat[n]) + 1;

				newNode.tForm[newNode.tCount].type = DT_MATRIX;
				newNode.tForm[newNode.tCount].sid = t.attribute("sid").value();
				memcpy(newNode.tForm[newNode.tCount].data, &tempTransform.mat[0], sizeof(float) * 16);

				tempTransform = tempTransform.transpose();
				newNode.tCount++;
			}
			else if(strcmp(t.name(), "rotate") == 0)
			{
				float rotateData[4];

				const char* str = t.child_value();

				for(int n = 0; n < 4; n++)
					str = fastatof(str, rotateData[n]) + 1;

				newNode.tForm[newNode.tCount].type = DT_ROTATE;
				newNode.tForm[newNode.tCount].sid = t.attribute("sid").value();
				memcpy(newNode.tForm[newNode.tCount].data, rotateData, sizeof(float) * 4);

				tempTransform.rotate(vec3(rotateData), rotateData[3]);
				newNode.tCount++;
			}
			else if(strcmp(t.name(), "translate") == 0)
			{
				float translateData[3];

				const char* str = t.child_value();

				for(int n = 0; n < 3; n++)
					str = fastatof(str, translateData[n]) + 1;

				newNode.tForm[newNode.tCount].type = DT_TRANSLATE;
				newNode.tForm[newNode.tCount].sid = t.attribute("sid").value();
				memcpy(newNode.tForm[newNode.tCount].data, translateData, sizeof(float) * 3);

				tempTransform.translate(vec3(translateData));
				newNode.tCount++;
			}
			else if(strcmp(t.name(), "scale") == 0)
			{
				float scaleData[3];

				const char* str = t.child_value();

				for(int n = 0; n < 3; n++)
					str = fastatof(str, scaleData[n]) + 1;

				newNode.tForm[newNode.tCount].type = DT_SCALE;
				newNode.tForm[newNode.tCount].sid = t.attribute("sid").value();
				memcpy(newNode.tForm[newNode.tCount].data, scaleData, sizeof(float) * 3);

				tempTransform.scale(vec3(scaleData));
				newNode.tCount++;
			}
			else if(strcmp(t.name(), "lookat") == 0)
			{
				newNode.tForm[newNode.tCount].type = DT_LOOKAT;
				newNode.tForm[newNode.tCount].sid = t.attribute("sid").value();

				assert(!"<lookat> not implemented!");
				LogPrint("<lookat> not implemented!\r\n");
				newNode.tCount++;
			}
			else if(strcmp(t.name(), "skew") == 0)
			{
				newNode.tForm[newNode.tCount].type = DT_SKEW;
				newNode.tForm[newNode.tCount].sid = t.attribute("sid").value();

				assert(!"<skew> not implemented!");
				LogPrint("<skew> not implemented!\r\n");
				newNode.tCount++;
			}

			newNode.model *= tempTransform;
		}

		// Find corresponding geometry
		newNode.geometryID = -1;

		newNode.effectID = ~0u;

		pugi::xml_node geom = n.child("instance_geometry");

		if(geom)
		{
			const char* str = geom.attribute("url").value() + 1;

			for(unsigned i = 0; i < global.geoms.size(); i++)
			{
				if(strcmp(str, global.geoms[i]->ID) == 0)
				{
					newNode.geometryID = i;
					break;
				}
			}

			// Find material
			pugi::xml_node mat = geom.child("bind_material").child("technique_common").child("instance_material");
			auto matTarget = mat.attribute("target").value();

			if(*matTarget)
			{
				for(unsigned i = 0; i < global.materials.size(); i++)
				{
					if(strcmp(global.materials[i].ID, matTarget + 1) == 0)
						newNode.effectID = global.materials[i].effectID;
				}
			}
		}

		assert(depth != sDepth);

		if(depth == sDepth + 1)
		{
			newNode.parentNodeID = -1;
		}
		else
		{
			newNode.parentNodeID = stack[depth - sDepth - 1];
			global.nodes[newNode.parentNodeID].childCount++;
		}

		newNode.childCount = 0;

		newNode.controllerID = -1;
		newNode.skeletonID = -1;

		pugi::xml_node contrl = n.child("instance_controller");

		if(contrl)
		{
			// Find controller number
			const char* targetName = contrl.attribute("url").value() + 1;

			for(unsigned i = 0; i < global.contrls.size(); i++)
			{
				if(strcmp(targetName, global.contrls[i]->ID) == 0)
				{
					newNode.controllerID = i;
					break;
				}
			}

			if(newNode.controllerID == -1)
				LogOptional("Controller %s is referenced, but not found\r\n", targetName);

			// Find material
			pugi::xml_node mat = contrl.child("bind_material").child("technique_common").child("instance_material");

			auto matTarget = mat.attribute("target").value();

			if(*matTarget)
			{
				for(unsigned i = 0; i < global.materials.size(); i++)
				{
					if(strcmp(global.materials[i].ID, matTarget + 1) == 0)
						newNode.effectID = global.materials[i].effectID;
				}
			}

			// Find skeleton
			pugi::xml_node skeleton = contrl.child("skeleton");

			if(skeleton)
				LogOptional("Skeleton for %s starts from %s\r\n", targetName, skeleton.child_value() + 1);
			else
				LogOptional("Controller %s doesn't have a skeleton reference\r\n", targetName);

			newSkeleton.controllerID = newNode.controllerID;
			newSkeleton.jointCount = global.contrls[newNode.controllerID]->joints->count;
			newSkeleton.rootName = skeleton.child_value() + 1;
			newNode.skeletonID = unsigned(global.skeletons.size());

			global.skeletons.push_back(newSkeleton);
		}

		LogOptional("Node number: %d, ID: %s, name: %s, sid: %s\r\n", unsigned(global.nodes.size()), newNode.ID, newNode.name, newNode.sid);
		LogOptional("\tgeometryID: %d, effectID: %d, controllerID: %d, skeletonID: %d, parentNodeID: %d, childCount: %d\r\n  ", newNode.geometryID, newNode.effectID, newNode.controllerID, newNode.skeletonID, newNode.parentNodeID, newNode.childCount);
		for(int m = 0; m < 16; m++)
			LogOptional("%f; ", newNode.model.mat[m]);
		LogOptional("\r\n");

		global.nodes.push_back(newNode);
	}

	for(unsigned i = 0; i < global.skeletons.size(); i++)
	{
		DAESkeleton &cSkel = global.skeletons[i];

		// find root node
		unsigned nodeID = 0;

		while(nodeID < global.nodes.size() && strcmp(global.nodes[nodeID].ID, cSkel.rootName) != 0)
			nodeID++;

		if(nodeID == global.nodes.size())
		{
			LogOptional("Root node '%s' for skeleton %d not found, will search whole tree\r\n", cSkel.rootName, i);

			nodeID = 0;
		}

		// Find every joint position after root node
		for(unsigned n = 0; n < cSkel.jointCount; n++)
		{
			char* targetName = global.contrls[cSkel.controllerID]->joints->dataName[n];

			unsigned subID = nodeID;

			while(subID < global.nodes.size() && (global.nodes[subID].sid == NULL || strcmp(global.nodes[subID].sid, targetName) != 0))
				subID++;

			if(subID == global.nodes.size())
			{
				LogOptional("Joint node %s for skeleton %d not found in root node %s\r\n", targetName, i, cSkel.rootName);
				return false;
			}

			cSkel.nodeIDs[n] = subID;
			LogOptional("Skeleton %d, found joint %s at %d\r\n", i, targetName, subID);

			cSkel.bindShapeMat = global.contrls[cSkel.controllerID]->bindMat;
			cSkel.bindMat[n] = mat4(&global.contrls[cSkel.controllerID]->binds->dataFloat[n * 16]).transpose() * cSkel.bindShapeMat;
		}
	}

	// Find out the list of nodes that require animation
	struct TransformBlock
	{
		DAETransformBlock block;
		unsigned count;
	};

	global.animatedNodes.clear();

	std::vector<TransformBlock> idleMat;

	double longestAnim = 0.0;

	for(unsigned i = 0; i < anims.size(); i++)
	{
		auto &anim = anims[i];

		unsigned targetNode = -1;

		for(unsigned n = 0; n < global.nodes.size(); n++)
		{
			if(strcmp(global.nodes[n].ID, anim->targetNode) == 0)
			{
				targetNode = n;
				break;
			}
		}

		if(targetNode == -1)
		{
			LogOptional("There is no node (%s) for animation (%s)\r\n", anim->targetNode, anim->ID);
			continue;
		}

		unsigned found = ~0u;
		for(unsigned k = 0; k < global.animatedNodes.size(); k++)
		{
			if(global.animatedNodes[k] == targetNode)
				found = k;
		}

		if(found == ~0u)
		{
			anim->targetNodePos = global.animatedNodes.size();
			global.animatedNodes.push_back(targetNode);

			idleMat.push_back(TransformBlock());
			idleMat.back().count = global.nodes[targetNode].tCount;

			memcpy(idleMat.back().block, global.nodes[targetNode].tForm, sizeof(DAETransformBlock));
		}
		else
		{
			anim->targetNodePos = found;
		}

		LogOptional("Animation (%s) for node (%s) at position %d\r\n", anim->ID, anim->targetNode, anim->targetNodePos);

		float *inTime = anim->inSource->dataFloat;
		longestAnim = longestAnim > inTime[anim->dataCount - 1] ? longestAnim : inTime[anim->dataCount - 1];
		anim->lastSample = 0;
	}

	TransformBlock *tempCopy = new TransformBlock[idleMat.size()];
	mat4 *targetMat = new mat4[idleMat.size()];

	double startTime = 0.0, step = 1.0 / 30.0, currTime = startTime;
	LogPrint("Sampling animation in a period of [0.0, %f] with a %f second step\r\n", longestAnim, step);

	mat4 *result = new mat4[idleMat.size()*int(longestAnim / step + 1)];
	LogPrint("Result size is %d\r\n", sizeof(mat4) * idleMat.size()*int(longestAnim / step + 1));
	unsigned frame = 0;

	while(currTime < longestAnim)
	{
		// copy idle transformation
		memcpy(tempCopy, &idleMat[0], sizeof(TransformBlock)*idleMat.size());

		// now, every animation will make changes to transformation state
		for(unsigned i = 0; i < anims.size(); i++)
		{
			DAEAnimation &anim = *anims[i];

			float *inTime = anim.inSource ? anim.inSource->dataFloat : NULL;
			float *outValues = anim.outSource ? anim.outSource->dataFloat : NULL;

			DAESource *inTangent = anim.inTangentSource;
			DAESource *outTangent = anim.outTangentSource;

			while(anim.lastSample < anim.dataCount - 1 && inTime[anim.lastSample + 1] < currTime)
				anim.lastSample++;

			double mix = (currTime - inTime[anim.lastSample]) / (inTime[anim.lastSample + 1] - inTime[anim.lastSample]);

			// If the animation is over
			if(mix > 1.0)
				continue;	// Skip to the next

			// Ill-formed animation?
			if(anim.targetSID == NULL)
				continue;

			unsigned targetPart = -1;

			for(unsigned k = 0; k < tempCopy[anim.targetNodePos].count; k++)
			{
				if(tempCopy[anim.targetNodePos].block[k].sid && strcmp(tempCopy[anim.targetNodePos].block[k].sid, anim.targetSID) == 0)
				{
					targetPart = k;
					break;
				}
			}

			if(targetPart == -1)
			{
				LogOptional("\tTarget sid (%s) issued by animation (%s) wasn't found\r\n", anims[i]->targetSID, anims[i]->ID);
				continue;
			}

			// Change target data
			unsigned outStride = anim.outSource->stride;

			for(unsigned n = anim.compOffset; n < anim.compOffset + outStride; n++)
			{
				if(anim.interpolation[anim.lastSample] == LINEAR)
				{
					tempCopy[anim.targetNodePos].block[targetPart].data[n] =
						float(
							outValues[anim.lastSample * outStride + n - anim.compOffset] * (1.0 - mix) +
							outValues[(anim.lastSample + 1) * outStride + n - anim.compOffset] * mix
							);
				}
				else if(anim.interpolation[anim.lastSample] == BEZIER)
				{
					vec4 s(
						float((1.0 - mix) * (1.0 - mix) * (1.0 - mix)),
						float(3.0 * mix * (1.0 - mix) * (1.0 - mix)),
						float(3.0 * mix * mix * (1.0 - mix)),
						float(mix * mix * mix)
					);
					assert(outTangent->stride == outStride * 2 && inTangent->stride == outStride * 2);
					vec4 c(
						outValues[anim.lastSample * outStride + n - anim.compOffset],
						outTangent->dataFloat[anim.lastSample * outTangent->stride + n - anim.compOffset + 1],
						inTangent->dataFloat[(anim.lastSample + 1) * inTangent->stride + n - anim.compOffset + 1],
						outValues[(anim.lastSample + 1) * outStride + n - anim.compOffset]
					);
					tempCopy[anim.targetNodePos].block[targetPart].data[n] = dot(s, c);
				}
				else if(anim.interpolation[anim.lastSample] == STEP)
				{
					tempCopy[anim.targetNodePos].block[targetPart].data[n] = float(outValues[anim.lastSample * outStride + n - anim.compOffset]);
				}
				else
				{
					assert(!"unsupported interpolation");
				}
			}
		}

		// Clear matrices
		for(unsigned i = 0; i < idleMat.size(); i++)
			targetMat[i].identity();

		mat4 tempTransform;

		// Calculate matrices
		for(unsigned i = 0; i < idleMat.size(); i++)
		{
			for(unsigned n = 0; n < tempCopy[i].count; n++)
			{
				tempTransform.identity();

				switch(tempCopy[i].block[n].type)
				{
				case DT_MATRIX:
					memcpy(&tempTransform.mat[0], tempCopy[i].block[n].data, 16 * sizeof(float));
					tempTransform = tempTransform.transpose();
					break;
				case DT_ROTATE:
					tempTransform.rotate(vec3(tempCopy[i].block[n].data), tempCopy[i].block[n].data[3]);
					break;
				case DT_TRANSLATE:
					tempTransform.translate(vec3(tempCopy[i].block[n].data));
					break;
				case DT_SCALE:
					tempTransform.scale(vec3(tempCopy[i].block[n].data));
					break;
				case DT_LOOKAT:
				case DT_SKEW:
					break;
				}

				targetMat[i] *= tempTransform;
			}
		}

		for(unsigned i = 0; i < idleMat.size(); i++)
			result[frame * idleMat.size() + i] = targetMat[i];

		frame++;

		currTime += step;
	}

	global.matrixAnimation = result;
	global.animSampleCount = frame - 1;

	return true;
}

void SaveToBlob(std::vector<unsigned char> &blob, void* data, unsigned size)
{
	blob.insert(blob.end(), (unsigned char*)data, (unsigned char*)data + size);
}

void SaveGeometry(char* folderNameOut)
{
	// Find, what geometry have a skin attached to it, and create geometry index indirection map
	bool *hasController = new bool[global.geoms.size()];

	for(unsigned i = 0; i < global.geoms.size(); i++)
		hasController[i] = false;

	for(unsigned i = 0; i < global.contrls.size(); i++)
		hasController[global.contrls[i]->geometryID] = true;

	LogPrint("Saving geometry\r\n");

	global.geometryIDs = new const char*[global.geoms.size()];

	std::vector<unsigned char> blob;

	for(unsigned n = 0; n < global.geoms.size(); n++)
	{
		auto&& source = global.geoms[n];

		Export::GeometryInfo target;

		blob.clear();

		bool withController = hasController[n];

		if(withController)
			LogOptional("  Saving dynamic geometry %s\r\n", source->name);
		else
			LogOptional("  Saving static geometry %s\r\n", source->name);

		target.version = 1;
		target.formatComponents = withController ? 6 : 4;
		target.vertexCount = source->VB.size();
		target.vertexSize = withController ? sizeof(Vertex) + 12 : sizeof(Vertex);
		target.indexCount = source->IB.size();
		target.indexSize = 2;
		target.bounds = source->bounds;

		// Save geometry info
		SaveToBlob(blob, &target, sizeof(target));

		// Save format
		if(withController)
		{
			unsigned format[] = { Export::FCT_FLOAT3,
									Export::FCT_INT16_2N,
									Export::FCT_INT16_4N,
									Export::FCT_INT16_4N,
									Export::FCT_UINT8_4,
									Export::FCT_END };

			SaveToBlob(blob, format, sizeof(format));
		}
		else
		{
			unsigned format[] = { Export::FCT_FLOAT3,
									Export::FCT_INT16_2N,
									Export::FCT_INT16_4N,
									Export::FCT_END };

			SaveToBlob(blob, format, sizeof(format));
		}

		// Save vertices
		if(withController)
		{
			// Find controller
			unsigned c;
			for(c = 0; c < global.contrls.size(); c++)
			{
				if(global.contrls[c]->geometryID == n)
					break;
			}

			LogOptional("   Geometry controller %d\r\n", c);

			for(unsigned k = 0; k < source->VB.size(); k++)
			{
				SaveToBlob(blob, &source->VB[k], sizeof(Vertex));

				// Save 4 bone weights
				SaveToBlob(blob, &global.contrls[c]->exWeights[(source->posIndex[k]) * 4], 8);

				// Save 4 bone indices
				SaveToBlob(blob, &global.contrls[c]->exIndices[(source->posIndex[k]) * 4], 4);
			}
		}
		else
		{
			SaveToBlob(blob, &source->VB[0], sizeof(source->VB[0]) * source->VB.size());
		}

		// Saving indices
		SaveToBlob(blob, &source->IB[0], sizeof(source->IB[0]) * source->IB.size());

		// Compute hash
		global.geometryIDs[n] = source->ID;

		// Save file
		char buf[128];
		sprintf(buf, "%s/%s.bgi", folderNameOut, source->ID);

		if(FILE *fOut = fopen(buf, "wb"))
		{
			fwrite(blob.data(), 1, blob.size(), fOut);
			fclose(fOut);
		}
	}

	delete[] hasController;
}

void SaveNode(char* fileNameOut, unsigned node, Context &global);

void SaveFile(char* fileNameOut, char* folderNameOut)
{
	SaveGeometry(folderNameOut);

	SaveNode(fileNameOut, -1, global);

	for(unsigned i = 0; i < global.nodes.size(); i++)
	{
		if(global.nodes[i].parentNodeID != -1)
			continue;

		const char *pos = strstr(global.nodes[i].name, "_object");

		if(!pos || pos[strlen("_object")] != 0)
			continue;

		char name[512];
		strcpy(name, global.nodes[i].name);

		char *pos1 = strstr(name, "_object");
		if(pos1)
			*pos1 = 0;

		LogOptional(" Saving stand-alone object '%s' node %d\r\n", name, i);

		// Save file
		char buf[128];
		sprintf(buf, "%s/%s.bmi", folderNameOut, name);

		SaveNode(buf, i, global);
	}

	delete[] global.geometryIDs;
}

void FreeData()
{
	for(unsigned i = 0, l = unsigned(global.geoms.size()); i != l; i++)
	{
		global.geoms[i]->Free();
		delete global.geoms[i];
	}
	delete[] data;
}

bool ProcessFile(char* fileNameIn, char* fileNameOut, char* folderNameOut)
{
	unsigned firstTime, startTime = firstTime = clock();
	if(!LoadFile(fileNameIn))
		return false;
	unsigned fileTime = clock() - startTime;

	startTime = clock();
	ParseFile(data);
	unsigned parseTime = clock() - startTime;

	startTime = clock();
	LoadMaterialLibrary();
	unsigned materialTime = clock() - startTime;

	startTime = clock();
	LoadAnimationLibrary();
	unsigned animTime = clock() - startTime;

	startTime = clock();
	LoadGeometryLibrary();
	unsigned dataloadTime = clock() - startTime;

	startTime = clock();
	CreateIBVB();
	unsigned vbibTime = clock() - startTime;

	startTime = clock();
	LoadControllerLibrary();
	unsigned skinTime = clock() - startTime;

	startTime = clock();
	bool sceneLoaded = LoadScene();
	if(!sceneLoaded)
		return false;
	unsigned nodeTime = clock() - startTime;

	startTime = clock();
	SaveFile(fileNameOut, folderNameOut);
	unsigned saveTime = clock() - startTime;

	startTime = clock();
	FreeData();

	LogPrint("%dms file reading time\r\n", fileTime);
	LogPrint("%dms for parsing\r\n%dms for data load, %dms VB IB creation time\r\n", parseTime, dataloadTime, vbibTime);
	LogPrint("%dms to parse animations, %dms to parse skin info, %dms to parse nodes, %dms to save the file\r\n", animTime, skinTime, nodeTime, saveTime);
	LogPrint("%dms to parse effects, %dms All\r\n", materialTime, clock() - firstTime);

	return true;
}

int main(unsigned argc, char** argv)
{
	logFile = fopen("log.txt", "wb");

	for(unsigned i = 1; i < argc; i++)
	{
		LogPrint("Processing %s ...\r\n", argv[i]);

		if(strstr(argv[i], ".dae") == NULL && strstr(argv[i], ".DAE") == NULL)
		{
			LogPrint("Wrong file format, skipping");
			continue;
		}

		char temp[512];
		strcpy(temp, argv[i]);

		for(unsigned k = 0, ke = strlen(temp); k < ke; k++)
		{
			if(temp[k] == '\\')
				temp[k] = '/';
		}

		char *folder = temp;

		char* spos1 = strrchr(folder, '/');

		if(spos1)
		{
			*spos1 = 0;
			spos1++;
		}
		else
		{
			folder = ".";
			spos1 = temp;
		}

		char newName[512];

		sprintf(newName, "%s/%s", folder, spos1);

		char* cpos1 = strrchr(newName, '.');
		memcpy(cpos1 + 1, "bmi", 3);

		ProcessFile(argv[i], newName, folder);
	}

	fclose(logFile);
	return 0;
}
