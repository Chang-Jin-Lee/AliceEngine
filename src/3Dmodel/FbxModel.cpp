#include "FbxModel.h"
#include "FbxMaterial.h"
#include "FbxGeometry.h"
#include "FbxSkeleton.h"
#include "FbxAnimation.h"
#include "../Core/Helper.h"
#include "../Core/Logger.h"
#include "../Core/ResourceManager.h"

#include <filesystem>
#include <ranges>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <queue>

using namespace DirectX;

struct FbxModel::Impl
{
	// Core
	std::unique_ptr<Assimp::Importer> importer;
	const aiScene* scene = nullptr; // owned by Assimp
	XMFLOAT4X4 globalInverse{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

	// Subsystems
	FbxMaterialLoader materials;
	FbxGeometryBuilder geometry;
	FbxSkeleton skeleton;
	FbxAnimation anim;

	// Maps for convenience
	std::unordered_map<std::string,int> nodeIndexOfName; // same as skeleton.NodeIndexOfName

	FbxModel::AnimationType animType = FbxModel::AnimationType::None;

	// Local bounds (computed from CPU vertices)
	bool     boundsValid = false;
	XMFLOAT3 boundsMin{ 0,0,0 };
	XMFLOAT3 boundsMax{ 0,0,0 };
};

FbxModel::FbxModel() : m_(new Impl) {}
FbxModel::~FbxModel() { Release(); }

void FbxModel::Release()
{
	m_->materials.Clear();
	m_->geometry.Clear();
	m_->skeleton = FbxSkeleton{};
	m_->anim.Clear();
	m_->nodeIndexOfName.clear();
	m_->scene = nullptr;
	m_->importer.reset();
	m_->animType = AnimationType::None;
	m_->boundsValid = false;
	m_->boundsMin = { 0,0,0 };
	m_->boundsMax = { 0,0,0 };
}

namespace
{
	static void ComputeLocalBoundsFromVertices(const std::vector<VertexSkinnedTBN>& verts,
	                                          bool& ioValid,
	                                          DirectX::XMFLOAT3& ioMin,
	                                          DirectX::XMFLOAT3& ioMax)
	{
		if (verts.empty())
		{
			ioValid = false;
			ioMin = { 0,0,0 };
			ioMax = { 0,0,0 };
			return;
		}

		DirectX::XMFLOAT3 mn{ FLT_MAX, FLT_MAX, FLT_MAX };
		DirectX::XMFLOAT3 mx{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (const auto& v : verts)
		{
			mn.x = (std::min)(mn.x, v.pos.x); mn.y = (std::min)(mn.y, v.pos.y); mn.z = (std::min)(mn.z, v.pos.z);
			mx.x = (std::max)(mx.x, v.pos.x); mx.y = (std::max)(mx.y, v.pos.y); mx.z = (std::max)(mx.z, v.pos.z);
		}
		ioValid = true;
		ioMin = mn;
		ioMax = mx;
	}
}

// pathW占쏙옙 占쏙옙占쏙옙占싸곤옙 占쏙옙占승댐옙.
bool FbxModel::Load(ID3D11Device* device, const std::wstring& pathW)
{
	Release();
	m_->importer = std::make_unique<Assimp::Importer>();
    // FBX 占실뱄옙/占쏙옙占쏙옙/占쏙옙占쏙옙트 회占쏙옙 占쏙옙占쏙옙占쏙옙 占쏙옙占쏙옙 Assimp占쏙옙 占쏙옙占쏙옙占싹댐옙 _$AssimpFbx$* 占쏙옙占쏙옙 占쏙옙弱?占쏙옙占신되억옙
    // 占쏙옙/占쏙옙占?占쏙옙占쏙옙 DCC(Blender)占쏙옙 占쏙옙 占쏙옙치占싹곤옙 占싯니댐옙.
    m_->importer->SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
	m_->importer->SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);
	std::string pathA = Utf8FromWString(pathW);
	m_->scene = m_->importer->ReadFile(pathA,
		aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality |
		aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_ConvertToLeftHanded |
		aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph | aiProcess_LimitBoneWeights);
	if (!m_->scene || !m_->scene->HasMeshes()) return false;

	// Global inverse
	{
		aiMatrix4x4 I = m_->scene->mRootNode->mTransformation; I.Inverse();
		m_->globalInverse._11 = (float)I.a1; m_->globalInverse._12 = (float)I.a2; m_->globalInverse._13 = (float)I.a3; m_->globalInverse._14 = (float)I.a4;
		m_->globalInverse._21 = (float)I.b1; m_->globalInverse._22 = (float)I.b2; m_->globalInverse._23 = (float)I.b3; m_->globalInverse._24 = (float)I.b4;
		m_->globalInverse._31 = (float)I.c1; m_->globalInverse._32 = (float)I.c2; m_->globalInverse._33 = (float)I.c3; m_->globalInverse._34 = (float)I.c4;
		m_->globalInverse._41 = (float)I.d1; m_->globalInverse._42 = (float)I.d2; m_->globalInverse._43 = (float)I.d3; m_->globalInverse._44 = (float)I.d4;
	}

	// Base dir
	// L"D:\\Project\\Resource\\Models\\player.fbx";
	// -> L"D:\\Project\\Resource\\Models\\" 
	auto baseDir = std::filesystem::path(pathW).parent_path().wstring();

	// Build subsystems
	// 占쌔댐옙占싹댐옙 占쏙옙占쏙옙占쏙옙 占쌍댐옙 占쏙옙占?占쌔쏙옙占식몌옙 占싻어봄.
	if (!m_->materials.Load(device, m_->scene, baseDir)) return false;
	if (!m_->geometry.Build(device, m_->scene)) return false;
	m_->skeleton.BuildFromScene(m_->scene);
	m_->skeleton.CollectBonesAndOffsets(m_->scene);
	m_->nodeIndexOfName = m_->skeleton.NodeIndexOfName();

	// Decide animation mode and prepare
	bool hasBones = m_->skeleton.HasBones();
	// 占쏙옙占쏙옙 占쏙옙占승듸옙 占쌍니몌옙占싱쇽옙占쏙옙 占쌍는곤옙占? 占쏙옙 占쏙옙占쏙옙占쏙옙 占쌍니몌옙占싱쇽옙占싹띰옙
	if (!hasBones && m_->scene->mNumAnimations > 0)
	{
		m_->animType = AnimationType::Rigid;
		m_->skeleton.BuildRigidBones();
		// Build rigid weights from per-vertex owning nodes so GPU skinning path can be reused
		{
			auto& verts = m_->geometry.GetCPUVertices();
			const auto& owners = m_->geometry.GetVertexOwningNodeNames();
			const auto& boneNames = m_->skeleton.GetBoneNames();
			const auto& skelNodes = m_->skeleton.GetSkeleton();
			std::unordered_map<std::string,int> boneIndexOfName;
			boneIndexOfName.reserve(boneNames.size());
			for (int i = 0; i < (int)boneNames.size(); ++i) boneIndexOfName[boneNames[(size_t)i]] = i;
			if (!verts.empty())
			{
				for (size_t i = 0; i < verts.size(); ++i)
				{
					unsigned short bi = 0;
					if (i < owners.size())
					{
						const std::string& owner = owners[i];
						auto itB = boneIndexOfName.find(owner);
						if (itB != boneIndexOfName.end()) bi = (unsigned short)itB->second;
						else
						{
							auto itNode = m_->nodeIndexOfName.find(owner);
							int node = (itNode != m_->nodeIndexOfName.end()) ? itNode->second : -1;
							while (node >= 0)
							{
								const auto& sn = skelNodes[(size_t)node];
								auto itB2 = boneIndexOfName.find(sn.name);
								if (itB2 != boneIndexOfName.end()) { bi = (unsigned short)itB2->second; break; }
								node = sn.parent;
							}
						}
					}
					verts[i].boneIdx[0] = bi; verts[i].boneIdx[1] = verts[i].boneIdx[2] = verts[i].boneIdx[3] = 0;
					verts[i].boneWeight = { 1.0f, 0.0f, 0.0f, 0.0f };
				}
				m_->geometry.RebuildVBFromCPU(device);
			}
		}
	}
	// 占쏙옙占쏙옙 占쌍곤옙 占쌍니몌옙占싱션듸옙 占쌍댐옙 占쏙옙占? Skinned 占쌍니몌옙占싱쇽옙 占싹띰옙.
	else if (hasBones)
	{
		m_->animType = AnimationType::Skinned;
		// Build skinning weights and re-upload VB so skinned VS works
		// IMPORTANT: BLENDINDICES must index into the palette ordered by boneNames (not node index)
		const auto& boneNames = m_->skeleton.GetBoneNames();
		std::unordered_map<std::string,int> boneIndexOfBoneName;
		boneIndexOfBoneName.reserve(boneNames.size());
		for (int i = 0; i < (int)boneNames.size(); ++i) boneIndexOfBoneName[boneNames[(size_t)i]] = i;
		auto& verts = m_->geometry.GetCPUVertices();
		if (!verts.empty())
		{
			struct Influence4 { unsigned short idx[4] = {0,0,0,0}; float w[4] = {0,0,0,0}; };
			std::vector<Influence4> inf; inf.assign(verts.size(), {});
			// Build base vertex table (mesh -> start in aggregated array) using same traversal order as geometry
			std::vector<size_t> baseVertex; baseVertex.resize(m_->scene->mNumMeshes, 0);
			size_t cursor = 0;
			std::function<void(const aiNode*)> fillBase = [&](const aiNode* node){
				for (unsigned mi = 0; mi < node->mNumMeshes; ++mi)
				{
					unsigned meshIdx = node->mMeshes[mi];
					baseVertex[meshIdx] = cursor;
					cursor += m_->scene->mMeshes[meshIdx]->mNumVertices;
				}
				for (unsigned ci = 0; ci < node->mNumChildren; ++ci) fillBase(node->mChildren[ci]);
			};
			fillBase(m_->scene->mRootNode);
			/*std::queue<const aiNode*> q;
			q.push(m_->scene->mRootNode);*/

			//while (!q.empty()) {
			//	const aiNode* node = q.front(); q.pop();
			//	// 占쌨쏙옙 처占쏙옙
			//	// NOTE:
			//	// - node->mMeshes 占쏙옙 "占쌨쏙옙 占싸듸옙占쏙옙 占썼열"占쌉니댐옙.
			//	// - std::views::counted(node->mMeshes, node->mNumMeshes) 占쏙옙 for-each 占싹몌옙
			//	//   mi 占쏙옙체占쏙옙 meshIdx 占쏙옙占싸듸옙, 占싣뤄옙占쏙옙占쏙옙 node->mMeshes[mi] 占쏙옙 占쌕쏙옙 占싸듸옙占쏙옙占싹몌옙
			//	//   占쌩몌옙占쏙옙 占쌨모리몌옙 占쏙옙占쏙옙占싹울옙 baseVertex 占쏙옙占싱븝옙占쏙옙 占쏙옙占쏙옙占쏙옙, 占쏙옙占쏙옙占쏙옙占쏙옙占?占쏙옙키占쏙옙 占쏙옙占쏙옙치占쏙옙
			//	//   占쏙옙占쏙옙占쏙옙 占쏙옙占쏙옙占쏙옙 占쏙옙占싸되억옙 占쌨시곤옙 '占쏙옙챗占쏙옙/占쏙옙占쏙옙'처占쏙옙 占쏙옙占쏙옙占쏙옙占싹댐옙.
			//	// - D3D11-AliceTutorial/31_IBL(App.cpp)占쏙옙 占쏙옙占시놂옙占? 占싸듸옙占쏙옙(0..mNumMeshes-1)占쏙옙 占쏙옙회占쌌니댐옙.
			//	for (unsigned mi = 0; mi < node->mNumMeshes; ++mi)
			//	{
			//		const unsigned meshIdx = node->mMeshes[mi];
			//		baseVertex[meshIdx] = cursor;
			//		cursor += m_->scene->mMeshes[meshIdx]->mNumVertices;
			//	}
			//	// 占쌘쏙옙 占쏙옙占?큐占쏙옙 占쌩곤옙
			//	for (const aiNode* child : std::views::counted(node->mChildren, node->mNumChildren)) {
			//		q.push(child);
			//	}
			//}

			for (unsigned mi = 0; mi < m_->scene->mNumMeshes; ++mi)
			{
				const aiMesh* mesh = m_->scene->mMeshes[mi];
				size_t base = baseVertex[mi];
				for (unsigned bi = 0; bi < mesh->mNumBones; ++bi)
				{
					const aiBone* b = mesh->mBones[bi];
					int boneIdx = -1;
					auto itB = boneIndexOfBoneName.find(b->mName.C_Str());
					if (itB != boneIndexOfBoneName.end()) boneIdx = itB->second;
					if (boneIdx < 0) continue;
					for (unsigned wi = 0; wi < b->mNumWeights; ++wi)
					{
						const aiVertexWeight& vw = b->mWeights[wi];
						size_t v = base + (size_t)vw.mVertexId;
						if (v >= inf.size()) continue;

						//int slot = 0; float minW = inf[v].w[0];
						//for (int s = 1; s < 4; ++s) { if (inf[v].w[s] < minW) { minW = inf[v].w[s]; slot = s; } }
						auto min_it = std::min_element(std::begin(inf[v].w), std::end(inf[v].w));
						int slot = (int)std::distance(std::begin(inf[v].w), min_it);

						inf[v].idx[slot] = (unsigned short)boneIdx;
						inf[v].w[slot] = (float)vw.mWeight;
					}
				}
			}
			// Normalize and apply to vertices
			for (size_t i = 0; i < inf.size(); ++i)
			{
				float s = inf[i].w[0] + inf[i].w[1] + inf[i].w[2] + inf[i].w[3];
				if (s > 1e-6f) { float inv = 1.0f / s; for (int k = 0; k < 4; ++k) inf[i].w[k] *= inv; }
				verts[i].boneIdx[0] = inf[i].idx[0];
				verts[i].boneIdx[1] = inf[i].idx[1];
				verts[i].boneIdx[2] = inf[i].idx[2];
				verts[i].boneIdx[3] = inf[i].idx[3];
				verts[i].boneWeight = { inf[i].w[0], inf[i].w[1], inf[i].w[2], inf[i].w[3] };
			}

			// === Debug: 占쏙옙키占쏙옙 占싸듸옙占쏙옙/占쏙옙占쏙옙치占쏙옙 占쏙옙占쏙옙 占쏙옙占쏙옙占쏙옙占쏙옙 占쏙옙占쏙옙占쏙옙 확占쏙옙 ===
			// {
			// 	const auto& boneNamesDbg = m_->skeleton.GetBoneNames();
			// 	unsigned short maxIdx = 0;
			// 	double maxWeightSum = 0.0;
			// 	size_t zeroWeightVerts = 0;
			// 	for (size_t i = 0; i < verts.size(); ++i)
			// 	{
			// 		const auto& v = verts[i];
			// 		maxIdx = (std::max)(maxIdx, v.boneIdx[0]);
			// 		maxIdx = (std::max)(maxIdx, v.boneIdx[1]);
			// 		maxIdx = (std::max)(maxIdx, v.boneIdx[2]);
			// 		maxIdx = (std::max)(maxIdx, v.boneIdx[3]);
			// 		const double ws = (double)v.boneWeight.x + (double)v.boneWeight.y + (double)v.boneWeight.z + (double)v.boneWeight.w;
			// 		maxWeightSum = (std::max)(maxWeightSum, ws);
			// 		if (ws < 1e-6) ++zeroWeightVerts;
			// 	}
			// 	ALICE_LOG_INFO("[FbxModel] SkinWeights: verts=%zu bones=%zu maxBoneIdx=%u zeroWeightVerts=%zu maxWeightSum=%.4f",
			// 		verts.size(), boneNamesDbg.size(), (unsigned)maxIdx, zeroWeightVerts, maxWeightSum);
			// 	for (size_t i = 0; i < (std::min<size_t>)(5, verts.size()); ++i)
			// 	{
			// 		const auto& v = verts[i];
			// 		ALICE_LOG_INFO("[FbxModel] v%zu idx=(%u,%u,%u,%u) w=(%.3f,%.3f,%.3f,%.3f)",
			// 			i,
			// 			(unsigned)v.boneIdx[0], (unsigned)v.boneIdx[1], (unsigned)v.boneIdx[2], (unsigned)v.boneIdx[3],
			// 			v.boneWeight.x, v.boneWeight.y, v.boneWeight.z, v.boneWeight.w);
			// 	}
			// }

			m_->geometry.RebuildVBFromCPU(device);
		}
	}
	// 占쏙옙占쏙옙 占쏙옙占쏙옙 占쌍니몌옙占싱션듸옙 占쏙옙占쏙옙占쏙옙. 占쏙옙 Static占쏙옙 占쏙옙占싹띰옙.
	else
	{
		m_->animType = AnimationType::None;
		// 占쏙옙키占쏙옙 占쏙옙占싱댐옙占쏙옙 占쏙옙占쏙옙占싹깍옙 占쏙옙占쏙옙, 占쏙옙占?占쏙옙占쏙옙占쏙옙 0占쏙옙 占쏙옙(Identity)占쏙옙 占쏙옙占쏙옙占쏙옙
		// 占쏙옙占쏙옙치(Weight)占쏙옙 0占싱몌옙 화占썽에 占쌓뤄옙占쏙옙占쏙옙 占쏙옙占쏙옙占실뤄옙 1.0占쏙옙占쏙옙 占쏙옙占쏙옙占쌔억옙 占쏙옙.
		auto& verts = m_->geometry.GetCPUVertices();
		if (!verts.empty())
		{
			for (auto& v : verts)
			{
				// 0占쏙옙 占쏙옙 占싸듸옙占쏙옙 占쏙옙占?Identity 占쏙옙占?
				v.boneIdx[0] = 0;
				v.boneIdx[1] = 0;
				v.boneIdx[2] = 0;
				v.boneIdx[3] = 0;

				// 첫 占쏙옙째 占쏙옙占쏙옙 占쏙옙占쏙옙치 100% 占쌀댐옙
				v.boneWeight = DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f);
			}
		}

		// 占쏙옙占쏙옙占?占쏙옙占쏙옙 占쏙옙占쏙옙占싶몌옙 GPU 占쏙옙占쌜울옙 占쌕쏙옙 占쏙옙占싸듸옙
		m_->geometry.RebuildVBFromCPU(device);
	}

	// Init animation metadata and ensure CB
	m_->anim.InitMetadata(m_->scene);
	m_->anim.SetType((m_->animType == AnimationType::Rigid) ? FbxAnimation::AnimType::Rigid : (m_->animType == AnimationType::Skinned ? FbxAnimation::AnimType::Skinned : FbxAnimation::AnimType::None));
	m_->anim.EnsureBoneCB(device, 1023);

	// Compute local AABB from CPU vertices (bind pose positions)
	{
		const auto& verts = m_->geometry.GetCPUVertices();
		ComputeLocalBoundsFromVertices(verts, m_->boundsValid, m_->boundsMin, m_->boundsMax);
	}
	return true;
}

bool FbxModel::LoadFromMemory(ID3D11Device* device,
                              const void* data,
                              size_t size,
                              const std::string& virtualNameUtf8,
                              const std::wstring& baseDirW)
{
	Release();
	if (!device || !data || size == 0) return false;
	
	m_->importer = std::make_unique<Assimp::Importer>();
	m_->importer->SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
	m_->importer->SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);

	const unsigned int flags =
		aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality |
		aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_ConvertToLeftHanded |
		aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph | aiProcess_LimitBoneWeights;

	// pHint 占쏙옙 확占쏙옙占쏙옙 占쏙옙트(占쏙옙: "fbx")占쏙옙 占쏙옙占쌉니댐옙.
	const char* hint = nullptr;
	std::string ext;
	{
		const auto dot = virtualNameUtf8.find_last_of('.');
		if (dot != std::string::npos && dot + 1 < virtualNameUtf8.size())
		{
			ext = virtualNameUtf8.substr(dot + 1);
			hint = ext.c_str();
		}
	}

	m_->scene = m_->importer->ReadFileFromMemory(data, size, flags, hint);
	if (!m_->scene || !m_->scene->HasMeshes()) return false;

	// Global inverse
	{
		aiMatrix4x4 I = m_->scene->mRootNode->mTransformation; I.Inverse();
		m_->globalInverse._11 = (float)I.a1; m_->globalInverse._12 = (float)I.a2; m_->globalInverse._13 = (float)I.a3; m_->globalInverse._14 = (float)I.a4;
		m_->globalInverse._21 = (float)I.b1; m_->globalInverse._22 = (float)I.b2; m_->globalInverse._23 = (float)I.b3; m_->globalInverse._24 = (float)I.b4;
		m_->globalInverse._31 = (float)I.c1; m_->globalInverse._32 = (float)I.c2; m_->globalInverse._33 = (float)I.c3; m_->globalInverse._34 = (float)I.c4;
		m_->globalInverse._41 = (float)I.d1; m_->globalInverse._42 = (float)I.d2; m_->globalInverse._43 = (float)I.d3; m_->globalInverse._44 = (float)I.d4;
	}

	// Build subsystems
	if (!m_->materials.Load(device, m_->scene, baseDirW)) return false;
	if (!m_->geometry.Build(device, m_->scene)) return false;
	m_->skeleton.BuildFromScene(m_->scene);
	m_->skeleton.CollectBonesAndOffsets(m_->scene);
	m_->nodeIndexOfName = m_->skeleton.NodeIndexOfName();

	// Decide animation mode and prepare (Load()占쏙옙 占쏙옙占쏙옙)
	bool hasBones = m_->skeleton.HasBones();
	if (!hasBones && m_->scene->mNumAnimations > 0)
	{
		m_->skeleton.BuildRigidBones();
		{
			auto& verts = m_->geometry.GetCPUVertices();
			const auto& owners = m_->geometry.GetVertexOwningNodeNames();
			const auto& boneNames = m_->skeleton.GetBoneNames();
			const auto& skelNodes = m_->skeleton.GetSkeleton();
			std::unordered_map<std::string,int> boneIndexOfName;
			boneIndexOfName.reserve(boneNames.size());
			for (int i = 0; i < (int)boneNames.size(); ++i) boneIndexOfName[boneNames[(size_t)i]] = i;
			if (!verts.empty())
			{
				for (size_t i = 0; i < verts.size(); ++i)
				{
					unsigned short bi = 0;
					if (i < owners.size())
					{
						const std::string& owner = owners[i];
						auto itB = boneIndexOfName.find(owner);
						if (itB != boneIndexOfName.end()) bi = (unsigned short)itB->second;
						else
						{
							auto itNode = m_->nodeIndexOfName.find(owner);
							int node = (itNode != m_->nodeIndexOfName.end()) ? itNode->second : -1;
							while (node >= 0)
							{
								const auto& sn = skelNodes[(size_t)node];
								auto itB2 = boneIndexOfName.find(sn.name);
								if (itB2 != boneIndexOfName.end()) { bi = (unsigned short)itB2->second; break; }
								node = sn.parent;
							}
						}
					}
					verts[i].boneIdx[0] = bi; verts[i].boneIdx[1] = verts[i].boneIdx[2] = verts[i].boneIdx[3] = 0;
					verts[i].boneWeight = { 1.0f, 0.0f, 0.0f, 0.0f };
				}
				m_->geometry.RebuildVBFromCPU(device);
			}
		}
		m_->animType = AnimationType::Rigid;
	}
	else if (hasBones)
	{
		m_->animType = AnimationType::Skinned;
		const auto& boneNames = m_->skeleton.GetBoneNames();
		std::unordered_map<std::string,int> boneIndexOfBoneName;
		boneIndexOfBoneName.reserve(boneNames.size());
		for (int i = 0; i < (int)boneNames.size(); ++i) boneIndexOfBoneName[boneNames[(size_t)i]] = i;
		auto& verts = m_->geometry.GetCPUVertices();
		if (!verts.empty())
		{
			struct Influence4 { unsigned short idx[4] = {0,0,0,0}; float w[4] = {0,0,0,0}; };
			std::vector<Influence4> inf; inf.assign(verts.size(), {});
			std::vector<size_t> baseVertex; baseVertex.resize(m_->scene->mNumMeshes, 0);
			size_t cursor = 0;
			std::function<void(const aiNode*)> fillBase = [&](const aiNode* node){
				for (unsigned mi = 0; mi < node->mNumMeshes; ++mi)
				{
					unsigned meshIdx = node->mMeshes[mi];
					baseVertex[meshIdx] = cursor;
					cursor += m_->scene->mMeshes[meshIdx]->mNumVertices;
				}
				for (unsigned ci = 0; ci < node->mNumChildren; ++ci) fillBase(node->mChildren[ci]);
			};
			fillBase(m_->scene->mRootNode);

			for (unsigned mi = 0; mi < m_->scene->mNumMeshes; ++mi)
			{
				const aiMesh* mesh = m_->scene->mMeshes[mi];
				size_t base = baseVertex[mi];
				for (unsigned bi = 0; bi < mesh->mNumBones; ++bi)
				{
					const aiBone* b = mesh->mBones[bi];
					int boneIdx = -1;
					auto itB = boneIndexOfBoneName.find(b->mName.C_Str());
					if (itB != boneIndexOfBoneName.end()) boneIdx = itB->second;
					if (boneIdx < 0) continue;
					for (unsigned wi = 0; wi < b->mNumWeights; ++wi)
					{
						const aiVertexWeight& vw = b->mWeights[wi];
						size_t v = base + (size_t)vw.mVertexId;
						if (v >= inf.size()) continue;
						int slot = 0; float minW = inf[v].w[0];
						for (int s = 1; s < 4; ++s) { if (inf[v].w[s] < minW) { minW = inf[v].w[s]; slot = s; } }
						inf[v].idx[slot] = (unsigned short)boneIdx;
						inf[v].w[slot] = (float)vw.mWeight;
					}
				}
			}
			for (size_t i = 0; i < inf.size(); ++i)
			{
				float s = inf[i].w[0] + inf[i].w[1] + inf[i].w[2] + inf[i].w[3];
				if (s > 1e-6f) { float inv = 1.0f / s; for (int k = 0; k < 4; ++k) inf[i].w[k] *= inv; }
				verts[i].boneIdx[0] = inf[i].idx[0];
				verts[i].boneIdx[1] = inf[i].idx[1];
				verts[i].boneIdx[2] = inf[i].idx[2];
				verts[i].boneIdx[3] = inf[i].idx[3];
				verts[i].boneWeight = { inf[i].w[0], inf[i].w[1], inf[i].w[2], inf[i].w[3] };
			}
			m_->geometry.RebuildVBFromCPU(device);
		}
	}
	else
	{
		m_->animType = AnimationType::None;

		// 占쏙옙키占쏙옙 占쏙옙占싱댐옙占쏙옙 占쏙옙占쏙옙占싹깍옙 占쏙옙占쏙옙, 占쏙옙占?占쏙옙占쏙옙占쏙옙 0占쏙옙 占쏙옙(Identity)占쏙옙 占쏙옙占쏙옙占쏙옙
		// 占쏙옙占쏙옙치(Weight)占쏙옙 0占싱몌옙 화占썽에 占쌓뤄옙占쏙옙占쏙옙 占쏙옙占쏙옙占실뤄옙 1.0占쏙옙占쏙옙 占쏙옙占쏙옙占쌔억옙 占쏙옙.
		auto& verts = m_->geometry.GetCPUVertices();
		if (!verts.empty())
		{
			for (auto& v : verts)
			{
				// 0占쏙옙 占쏙옙 占싸듸옙占쏙옙 占쏙옙占?Identity 占쏙옙占?
				v.boneIdx[0] = 0;
				v.boneIdx[1] = 0;
				v.boneIdx[2] = 0;
				v.boneIdx[3] = 0;

				// 첫 占쏙옙째 占쏙옙占쏙옙 占쏙옙占쏙옙치 100% 占쌀댐옙
				v.boneWeight = DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f);
			}
		}

		// 占쏙옙占쏙옙占?占쏙옙占쏙옙 占쏙옙占쏙옙占싶몌옙 GPU 占쏙옙占쌜울옙 占쌕쏙옙 占쏙옙占싸듸옙
		m_->geometry.RebuildVBFromCPU(device);
	}

	m_->anim.InitMetadata(m_->scene);
	m_->anim.SetType((m_->animType == AnimationType::Rigid) ? FbxAnimation::AnimType::Rigid : (m_->animType == AnimationType::Skinned ? FbxAnimation::AnimType::Skinned : FbxAnimation::AnimType::None));
	m_->anim.EnsureBoneCB(device, 1023);

	// Compute local AABB from CPU vertices (bind pose positions)
	{
		const auto& verts = m_->geometry.GetCPUVertices();
		ComputeLocalBoundsFromVertices(verts, m_->boundsValid, m_->boundsMin, m_->boundsMax);
	}
	return true;
}
bool FbxModel::Load(ID3D11Device* device, Alice::ResourceManager& rm, const std::filesystem::path& fbxLogicalPath)
{
	// 논리 경로를 실제 경로로 변환
	const std::filesystem::path resolved = rm.Resolve(fbxLogicalPath);
	if (resolved.empty())
		return false;

	// 기존 Load 함수 호출 (레거시 호환)
	return Load(device, resolved.wstring());
}

bool FbxModel::LoadFromMemory(ID3D11Device* device,
						      Alice::ResourceManager& rm,
                              const std::filesystem::path& fbxLogicalPath,
                              const void* data,
                              size_t size,
                              const std::string& virtualNameUtf8)
{
	Release();
	if (!device || !data || size == 0) return false;
	
	m_->importer = std::make_unique<Assimp::Importer>();
	m_->importer->SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
	m_->importer->SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);

	const unsigned int flags =
		aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality |
		aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_ConvertToLeftHanded |
		aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph | aiProcess_LimitBoneWeights;

	// pHint 는 확장자 추출 (예: "fbx")
	const char* hint = nullptr;
	std::string ext;
	{
		const auto dot = virtualNameUtf8.find_last_of('.');
		if (dot != std::string::npos && dot + 1 < virtualNameUtf8.size())
		{
			ext = virtualNameUtf8.substr(dot + 1);
			hint = ext.c_str();
		}
	}

	m_->scene = m_->importer->ReadFileFromMemory(data, size, flags, hint);
	if (!m_->scene || !m_->scene->HasMeshes()) return false;

	// Global inverse
	{
		aiMatrix4x4 I = m_->scene->mRootNode->mTransformation; I.Inverse();
		m_->globalInverse._11 = (float)I.a1; m_->globalInverse._12 = (float)I.a2; m_->globalInverse._13 = (float)I.a3; m_->globalInverse._14 = (float)I.a4;
		m_->globalInverse._21 = (float)I.b1; m_->globalInverse._22 = (float)I.b2; m_->globalInverse._23 = (float)I.b3; m_->globalInverse._24 = (float)I.b4;
		m_->globalInverse._31 = (float)I.c1; m_->globalInverse._32 = (float)I.c2; m_->globalInverse._33 = (float)I.c3; m_->globalInverse._34 = (float)I.c4;
		m_->globalInverse._41 = (float)I.d1; m_->globalInverse._42 = (float)I.d2; m_->globalInverse._43 = (float)I.d3; m_->globalInverse._44 = (float)I.d4;
	}

	// Build subsystems - ResourceManager 기반으로 텍스처 로드
	if (!m_->materials.Load(device, m_->scene, fbxLogicalPath, rm)) return false;
	if (!m_->geometry.Build(device, m_->scene)) return false;
	m_->skeleton.BuildFromScene(m_->scene);
	m_->skeleton.CollectBonesAndOffsets(m_->scene);
	m_->nodeIndexOfName = m_->skeleton.NodeIndexOfName();

	// Decide animation mode and prepare (기존 LoadFromMemory와 동일한 로직)
	bool hasBones = m_->skeleton.HasBones();
	if (!hasBones && m_->scene->mNumAnimations > 0)
	{
		m_->skeleton.BuildRigidBones();
		{
			auto& verts = m_->geometry.GetCPUVertices();
			const auto& owners = m_->geometry.GetVertexOwningNodeNames();
			const auto& boneNames = m_->skeleton.GetBoneNames();
			const auto& skelNodes = m_->skeleton.GetSkeleton();
			std::unordered_map<std::string,int> boneIndexOfName;
			boneIndexOfName.reserve(boneNames.size());
			for (int i = 0; i < (int)boneNames.size(); ++i) boneIndexOfName[boneNames[(size_t)i]] = i;
			if (!verts.empty())
			{
				for (size_t i = 0; i < verts.size(); ++i)
				{
					unsigned short bi = 0;
					if (i < owners.size())
					{
						const std::string& owner = owners[i];
						auto itB = boneIndexOfName.find(owner);
						if (itB != boneIndexOfName.end()) bi = (unsigned short)itB->second;
						else
						{
							auto itNode = m_->nodeIndexOfName.find(owner);
							int node = (itNode != m_->nodeIndexOfName.end()) ? itNode->second : -1;
							while (node >= 0)
							{
								const auto& sn = skelNodes[(size_t)node];
								auto itB2 = boneIndexOfName.find(sn.name);
								if (itB2 != boneIndexOfName.end()) { bi = (unsigned short)itB2->second; break; }
								node = sn.parent;
							}
						}
					}
					verts[i].boneIdx[0] = bi; verts[i].boneIdx[1] = verts[i].boneIdx[2] = verts[i].boneIdx[3] = 0;
					verts[i].boneWeight = { 1.0f, 0.0f, 0.0f, 0.0f };
				}
				m_->geometry.RebuildVBFromCPU(device);
			}
		}
		m_->animType = AnimationType::Rigid;
	}
	else if (hasBones)
	{
		m_->animType = AnimationType::Skinned;
		const auto& boneNames = m_->skeleton.GetBoneNames();
		std::unordered_map<std::string,int> boneIndexOfBoneName;
		boneIndexOfBoneName.reserve(boneNames.size());
		for (int i = 0; i < (int)boneNames.size(); ++i) boneIndexOfBoneName[boneNames[(size_t)i]] = i;
		auto& verts = m_->geometry.GetCPUVertices();
		if (!verts.empty())
		{
			struct Influence4 { unsigned short idx[4] = {0,0,0,0}; float w[4] = {0,0,0,0}; };
			std::vector<Influence4> inf; inf.assign(verts.size(), {});
			std::vector<size_t> baseVertex; baseVertex.resize(m_->scene->mNumMeshes, 0);
			size_t cursor = 0;
			std::function<void(const aiNode*)> fillBase = [&](const aiNode* node){
				for (unsigned mi = 0; mi < node->mNumMeshes; ++mi)
				{
					unsigned meshIdx = node->mMeshes[mi];
					baseVertex[meshIdx] = cursor;
					cursor += m_->scene->mMeshes[meshIdx]->mNumVertices;
				}
				for (unsigned ci = 0; ci < node->mNumChildren; ++ci) fillBase(node->mChildren[ci]);
			};
			fillBase(m_->scene->mRootNode);

			for (unsigned mi = 0; mi < m_->scene->mNumMeshes; ++mi)
			{
				const aiMesh* mesh = m_->scene->mMeshes[mi];
				size_t base = baseVertex[mi];
				for (unsigned bi = 0; bi < mesh->mNumBones; ++bi)
				{
					const aiBone* b = mesh->mBones[bi];
					int boneIdx = -1;
					auto itB = boneIndexOfBoneName.find(b->mName.C_Str());
					if (itB != boneIndexOfBoneName.end()) boneIdx = itB->second;
					if (boneIdx < 0) continue;
					for (unsigned wi = 0; wi < b->mNumWeights; ++wi)
					{
						const aiVertexWeight& vw = b->mWeights[wi];
						size_t v = base + (size_t)vw.mVertexId;
						if (v >= inf.size()) continue;
						int slot = 0; float minW = inf[v].w[0];
						for (int s = 1; s < 4; ++s) { if (inf[v].w[s] < minW) { minW = inf[v].w[s]; slot = s; } }
						inf[v].idx[slot] = (unsigned short)boneIdx;
						inf[v].w[slot] = (float)vw.mWeight;
					}
				}
			}
			for (size_t i = 0; i < inf.size(); ++i)
			{
				float s = inf[i].w[0] + inf[i].w[1] + inf[i].w[2] + inf[i].w[3];
				if (s > 1e-6f) { float inv = 1.0f / s; for (int k = 0; k < 4; ++k) inf[i].w[k] *= inv; }
				verts[i].boneIdx[0] = inf[i].idx[0];
				verts[i].boneIdx[1] = inf[i].idx[1];
				verts[i].boneIdx[2] = inf[i].idx[2];
				verts[i].boneIdx[3] = inf[i].idx[3];
				verts[i].boneWeight = { inf[i].w[0], inf[i].w[1], inf[i].w[2], inf[i].w[3] };
			}
			m_->geometry.RebuildVBFromCPU(device);
		}
	}
	else
	{
		m_->animType = AnimationType::None;

		// 본키가 없는 경우 본 인덱스를 0으로 설정(Identity 본)
		// 가중치(Weight)는 0으로 화면에 표시되면 안 되므로 첫 번째 가중치에 1.0을 설정
		auto& verts = m_->geometry.GetCPUVertices();
		if (!verts.empty())
		{
			for (auto& v : verts)
			{
				// 0번 본 인덱스는 Identity 본
				v.boneIdx[0] = 0;
				v.boneIdx[1] = 0;
				v.boneIdx[2] = 0;
				v.boneIdx[3] = 0;

				// 첫 번째 가중치에 100% 할당
				v.boneWeight = DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f);
			}
		}

		// 가중치를 GPU 버퍼로 다시 업로드
		m_->geometry.RebuildVBFromCPU(device);
	}

	m_->anim.InitMetadata(m_->scene);
	m_->anim.SetType((m_->animType == AnimationType::Rigid) ? FbxAnimation::AnimType::Rigid : (m_->animType == AnimationType::Skinned ? FbxAnimation::AnimType::Skinned : FbxAnimation::AnimType::None));
	m_->anim.EnsureBoneCB(device, 1023);

	// Compute local AABB from CPU vertices (bind pose positions)
	{
		const auto& verts = m_->geometry.GetCPUVertices();
		ComputeLocalBoundsFromVertices(verts, m_->boundsValid, m_->boundsMin, m_->boundsMax);
	}
	return true;
}

// Mesh getters
bool FbxModel::HasMesh() const { return m_->geometry.GetVB() && m_->geometry.GetIB() && m_->geometry.GetIndexCount() > 0; }
ID3D11Buffer* FbxModel::GetVertexBuffer() const { return m_->geometry.GetVB(); }
ID3D11Buffer* FbxModel::GetIndexBuffer() const { return m_->geometry.GetIB(); }
int FbxModel::GetIndexCount() const { return m_->geometry.GetIndexCount(); }
UINT FbxModel::GetVertexStride() const { return m_->geometry.GetVertexStride(); }
const std::vector<FbxSubset>& FbxModel::GetSubsets() const { return m_->geometry.GetSubsets(); }
const std::vector<ID3D11ShaderResourceView*>& FbxModel::GetMaterialSRVs() const { return m_->materials.GetMaterialSRVs(); }
const std::vector<ID3D11ShaderResourceView*>& FbxModel::GetNormalSRVs() const { return m_->materials.GetNormalSRVs(); }
const std::vector<ID3D11ShaderResourceView*>& FbxModel::GetMetallicSRVs() const { return m_->materials.GetMetallicSRVs(); }
const std::vector<ID3D11ShaderResourceView*>& FbxModel::GetRoughnessSRVs() const { return m_->materials.GetRoughnessSRVs(); }

// Skeleton/animation
bool FbxModel::HasSkeleton() const { return m_->skeleton.HasBones(); }
bool FbxModel::HasAnimations() const { return !m_->anim.GetNames().empty(); }
const std::vector<FbxSkeletonNode>& FbxModel::GetSkeleton() const { return m_->skeleton.GetSkeleton(); }
int FbxModel::GetSkeletonRoot() const { return m_->skeleton.GetRootIndex(); }
ID3D11Buffer* FbxModel::GetBoneConstantBuffer() const { return m_->anim.GetBoneCB(); }
UINT FbxModel::GetBoneCount() const { return (UINT)m_->skeleton.GetBoneNames().size(); }

const std::vector<std::string>& FbxModel::GetAnimationNames() const { return m_->anim.GetNames(); }
int FbxModel::GetCurrentAnimationIndex() const { return m_->anim.GetCurrentIndex(); }
void FbxModel::SetCurrentAnimation(int idx) { m_->anim.SetCurrentIndex(idx); }
void FbxModel::SetAnimationPlaying(bool playing) { m_->anim.SetPlaying(playing); }
bool FbxModel::IsAnimationPlaying() const { return m_->anim.IsPlaying(); }
double FbxModel::GetAnimationTimeSeconds() const { return m_->anim.GetTimeSec(); }
void FbxModel::SetAnimationTimeSeconds(double t) { m_->anim.SetTimeSec(t); }
void FbxModel::UpdateAnimation(ID3D11DeviceContext* ctx, double dtSec)
{
	m_->anim.UpdateAndUpload(
		ctx,
		dtSec,
		m_->scene,
		m_->nodeIndexOfName,
		m_->skeleton.GetBoneNames(),
		m_->skeleton.GetBoneOffsets(),
		m_->globalInverse);
}
double FbxModel::GetClipDurationSec(int idx) const { return m_->anim.GetClipDurationSec(idx); }

FbxModel::AnimationType FbxModel::GetCurrentAnimationType() const { return m_->animType; }

// Shared data accessors for per-instance animators
const aiScene* FbxModel::GetScenePtr() const { return m_->scene; }
const std::unordered_map<std::string,int>& FbxModel::GetNodeIndexOfName() const { return m_->nodeIndexOfName; }
const std::vector<std::string>& FbxModel::GetBoneNames() const { return m_->skeleton.GetBoneNames(); }
const std::vector<XMFLOAT4X4>& FbxModel::GetBoneOffsets() const { return m_->skeleton.GetBoneOffsets(); }
const XMFLOAT4X4& FbxModel::GetGlobalInverse() const { return m_->globalInverse; }

bool FbxModel::GetLocalBounds(DirectX::XMFLOAT3& outMin, DirectX::XMFLOAT3& outMax) const
{
	if (!m_ || !m_->boundsValid)
		return false;
	outMin = m_->boundsMin;
	outMax = m_->boundsMax;
	return true;
}


