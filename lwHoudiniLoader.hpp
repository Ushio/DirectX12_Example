#pragma once

#include "glm/glm.hpp"
#include "glm/ext.hpp"
#include <vector>
#include <map>
#include <stdexcept>

#include "rapidjson/document.h"
#include "rapidjson/rapidjson.h"

// #define LWH_EXPECT( v, message ) if( (v) == false ) { char buffer[512]; sprintf(buffer, "%s, %d line", message, __LINE__); throw std::runtime_error(std::string(buffer)); }

#include <intrin.h>
#define LWH_EXPECT( v, message ) if( (v) == false ) { char buffer[512]; printf(buffer, "%s, %d line", message, __LINE__); __debugbreak(); }

namespace lwh {
	struct Polygon
	{
	public:
		glm::mat4 xform = glm::identity<glm::mat4>();
		std::vector<glm::vec3> P;	   // Points
		std::vector<uint32_t> indices; // Vertices
		std::vector<uint32_t> indexPerPrim;

		// meta
		uint32_t pointCount = 0;
		uint32_t vertexCount = 0;
		uint32_t primitiveCount = 0;

		// attributes
		std::map<std::string, std::vector<glm::vec3>> pointsVectorAttrib;
		std::map<std::string, std::vector<glm::vec3>> verticesVectorAttrib;
		std::map<std::string, std::vector<glm::vec3>> primitivesVectorAttrib;
	};

	struct Loaded {
		Polygon* polygon = nullptr;
	};

	namespace details {
	static std::string GetMemberAsString(const rapidjson::Value& o, const char *key )
	{
		LWH_EXPECT(o.HasMember(key), "missing key");
		const rapidjson::Value& v = o[key];
		LWH_EXPECT(v.IsString(), "the type must be string");
		return v.GetString();
	}
	static glm::mat4 GetMemberAsXform(const rapidjson::Value& o, const char *key)
	{
		LWH_EXPECT(o.HasMember(key), "missing key");
		const rapidjson::Value& xform = o[key];
		LWH_EXPECT(xform.IsArray(), "type mismatch");
		LWH_EXPECT(xform.Size() == 16, "");
		glm::mat4 m;
		for (rapidjson::SizeType i = 0; i < xform.Size(); i++)
		{
			LWH_EXPECT(xform[i].IsNumber(), "");
			glm::value_ptr(m)[i] = xform[i].GetFloat();
		}
		return m;
	}
	static std::vector<glm::vec3> GetMemberAsVectors(const rapidjson::Value& o, const char *key)
	{
		std::vector<glm::vec3> values;

		LWH_EXPECT(o.HasMember(key), "missing key");
		const rapidjson::Value& vs = o[key];
		LWH_EXPECT(vs.IsArray(), "type mismatch");
		LWH_EXPECT(vs.Size() % 3 == 0, "type mismatch"); // xyz

		values.reserve(vs.Size() / 3);	// xyz
		for (rapidjson::SizeType i = 0; i < vs.Size(); i += 3)
		{
			LWH_EXPECT(vs[i].IsNumber(), "");
			LWH_EXPECT(vs[i + 1].IsNumber(), "");
			LWH_EXPECT(vs[i + 2].IsNumber(), "");
			float x = vs[i].GetFloat();
			float y = vs[i + 1].GetFloat();
			float z = vs[i + 2].GetFloat();
			values.push_back(glm::vec3(x, y, z));
		}

		return std::move(values);
	}
	static std::vector<uint32_t> GetMemberAsUIntegers(const rapidjson::Value& o, const char *key)
	{
		std::vector<uint32_t> values;

		LWH_EXPECT(o.HasMember(key), "missing key");
		const rapidjson::Value& vs = o[key];
		LWH_EXPECT(vs.IsArray(), "type mismatch");

		values.reserve(vs.Size());
		for (rapidjson::SizeType i = 0; i < vs.Size(); i++)
		{
			LWH_EXPECT(vs[i].IsNumber(), "");
			values.push_back(vs[i].GetUint());
		}

		return std::move(values);
	}
	static Polygon* loadPolygon( const rapidjson::Document& d )
	{
		Polygon *polygon = new Polygon();
		polygon->xform = GetMemberAsXform(d, "xform");

		LWH_EXPECT(d.HasMember("Points"), "missing key");
		const rapidjson::Value& Points = d["Points"];
		LWH_EXPECT(Points.IsObject(), "type mismatch");

		polygon->P = GetMemberAsVectors(Points, "P");

		PR_ASSERT(d.HasMember("Vertices"));
		const rapidjson::Value& Vertices = d["Vertices"];
		PR_ASSERT(Vertices.IsObject());

		polygon->indices = GetMemberAsUIntegers(Vertices, "Point Num");
		polygon->indexPerPrim = GetMemberAsUIntegers(Vertices, "Index Count");

		// Point Attributes
		{
			uint32_t pointCount = polygon->P.size();
			LWH_EXPECT(d.HasMember("Points"), "");

			for (auto it = Points.MemberBegin(); it != Points.MemberEnd(); ++it)
			{
				LWH_EXPECT(it->value.IsArray(), "");

				// vector
				if (pointCount * 3 == it->value.Size())
				{
					polygon->pointsVectorAttrib[it->name.GetString()] = GetMemberAsVectors(Points, it->name.GetString());
				}
				else if (pointCount == it->value.Size())
				{

				}
			}
			polygon->pointCount = pointCount;
		}

		{
			uint32_t vertexCount = polygon->indices.size();
			LWH_EXPECT(d.HasMember("Vertices"), "");
			for (auto it = Vertices.MemberBegin(); it != Vertices.MemberEnd(); ++it)
			{
				LWH_EXPECT(it->value.IsArray(), "");

				// vector
				if (vertexCount * 3 == it->value.Size())
				{
					polygon->verticesVectorAttrib[it->name.GetString()] = GetMemberAsVectors(Vertices, it->name.GetString());
				}
				else if (vertexCount == it->value.Size())
				{

				}
			}
			polygon->vertexCount = vertexCount;
		}

		{
			uint32_t primitiveCount = polygon->indices.size() / 3;
			LWH_EXPECT(d.HasMember("Primitives"), "");
			const rapidjson::Value& Primitives = d["Primitives"];
			LWH_EXPECT(Primitives.IsObject(), "");

			for (auto it = Primitives.MemberBegin(); it != Primitives.MemberEnd(); ++it)
			{
				LWH_EXPECT(it->value.IsArray(), "");

				// vector
				if (primitiveCount * 3 == it->value.Size())
				{
					polygon->primitivesVectorAttrib[it->name.GetString()] = GetMemberAsVectors(Primitives, it->name.GetString());
				}
				else if (primitiveCount == it->value.Size())
				{

				}
			}
			polygon->primitiveCount = polygon->indexPerPrim.size();
		}
		return polygon;
	}
	}

	static Loaded load( const rapidjson::Document& d ) 
	{
		Loaded r;
		using namespace details;

		std::string type = details::GetMemberAsString(d, "type");
		if (type == "Polygon")
		{
			r.polygon = loadPolygon(d);
		}
		return r;
	}
}