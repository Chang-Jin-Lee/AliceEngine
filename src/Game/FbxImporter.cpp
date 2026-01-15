#include "Game/FbxImporter.h"

#include <algorithm>
#include <system_error>
#include <cstring>
#include <fstream>
#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <DirectXMath.h>

#include <assimp/scene.h>
#include <assimp/material.h>

#include "Core/World.h"          // MaterialComponent 정의
#include "Core/Material.h"       // MaterialFile::Save
#include "Core/ResourceManager.h"
#include "Core/Logger.h"
#include "3Dmodel/FbxModel.h"
#include "Rendering/SkinnedMeshRegistry.h"   // SkinnedMeshGPU / SkinnedMeshRegistry
#include <Core/Helper.h>
#include "Game/FbxAsset.h"

namespace Alice
{
    namespace
    {
        inline char ToLowerChar(unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }

        inline void ToLowerInPlace(std::string& s)
        {
            for (char& c : s) c = ToLowerChar(static_cast<unsigned char>(c));
        }

        // 간단한 이미지 확장자 체크 함수입니다.
        inline bool IsImageFile(const std::filesystem::path& path)
        {
            const std::string ext = path.extension().string();
            if (ext.empty()) return false;

            std::string lower = ext;
            ToLowerInPlace(lower);

            return lower == ".png"  || lower == ".jpg"  || lower == ".jpeg" ||
                   lower == ".tga"  || lower == ".bmp"  || lower == ".dds";
        }

        inline bool EndsWith(std::string_view s, std::string_view suffix)
        {
            return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
        }

        // "...\Resource\<rel>" absolute 경로를 "Resource/<rel>" 로 최대한 정규화합니다.
        inline std::string NormalizeToResourceLogical(const std::filesystem::path& p)
        {
            if (!p.is_absolute())
                return p.generic_string();

            const std::string s = p.generic_string();
            std::string lower = s;
            ToLowerInPlace(lower);
            const std::string needle = "/resource/";
            const auto pos = lower.find(needle);
            if (pos == std::string::npos)
                return s;

            const std::string rel = s.substr(pos + needle.size());
            return (std::filesystem::path("Resource") / std::filesystem::path(rel)).generic_string();
        }

        static void AppendSkeletonText(const std::vector<FbxSkeletonNode>& nodes,
                                       int idx,
                                       int depth,
                                       std::string& text)
        {
            if (idx < 0)
                return;
            if (idx >= static_cast<int>(nodes.size()))
                return;

            const FbxSkeletonNode& n = nodes[static_cast<std::size_t>(idx)];
            text.append(static_cast<std::size_t>(depth * 2), ' ');
            text += n.name;
            text += "\n";

            for (int child : n.children)
                AppendSkeletonText(nodes, child, depth + 1, text);
        }

        static void ExtractTexture_FileMode(const aiScene* scene,
                                            aiMaterial* mat,
                                            aiTextureType type,
                                            const char* tag,
                                            const std::filesystem::path& fbxDir,
                                            const std::filesystem::path& fbmDir,
                                            const std::string& baseName,
                                            std::vector<std::filesystem::path>& extractedTextures)
        {
            namespace fs = std::filesystem;

            if (!scene || !mat)
                return;

            aiString texPath;
            if (mat->GetTexture(type, 0, &texPath) != AI_SUCCESS)
                return;

            if (std::strlen(texPath.C_Str()) == 0)
                return;

            const std::string t = texPath.C_Str();

            // fbmDir이 유효한지 확인
            std::error_code ec;
            if (!fs::exists(fbmDir, ec) || !fs::is_directory(fbmDir, ec))
            {
                ALICE_LOG_WARN("[FbxImporter] ExtractTexture_FileMode: fbmDir does not exist or is not a directory: \"%s\"", fbmDir.string().c_str());
                return;
            }

            // 임베디드 텍스처
            const aiTexture* at = scene->GetEmbeddedTexture(t.c_str());
            if (at)
            {
                static int embeddedIndex = 0;

                std::string ext = at->achFormatHint;
                if (ext.empty())
                    ext = (at->mHeight == 0) ? "bin" : "dds";

                try
                {
                    fs::path outPath = fbmDir / (baseName + "_" + tag + "_embedded" + std::to_string(embeddedIndex++) + "." + ext);
                    std::ofstream ofs(outPath, std::ios::binary);
                    if (!ofs.is_open())
                    {
                        ALICE_LOG_WARN("[FbxImporter] ExtractTexture_FileMode: failed to open output file: \"%s\"", outPath.string().c_str());
                        return;
                    }

                    if (at->mHeight == 0)
                    {
                        if (at->mWidth > 0)
                            ofs.write(reinterpret_cast<const char*>(at->pcData), static_cast<std::streamsize>(at->mWidth));
                    }
                    else
                    {
                        const std::size_t dataSize = static_cast<std::size_t>(at->mWidth) * static_cast<std::size_t>(at->mHeight) * sizeof(aiTexel);
                        if (dataSize > 0)
                            ofs.write(reinterpret_cast<const char*>(at->pcData), static_cast<std::streamsize>(dataSize));
                    }

                    if (ofs.good())
                        extractedTextures.push_back(outPath);
                }
                catch (const std::exception& e)
                {
                    ALICE_LOG_ERRORF("[FbxImporter] ExtractTexture_FileMode: exception while writing embedded texture: %s", e.what());
                }
                return;
            }

            // 외부 파일 텍스처 → .fbm 으로 복사
            try
            {
                fs::path srcTex = t;
                if (!srcTex.is_absolute())
                {
                    // fbxDir이 유효한지 확인
                    if (fbxDir.empty() || !fs::exists(fbxDir, ec) || !fs::is_directory(fbxDir, ec))
                    {
                        ALICE_LOG_WARN("[FbxImporter] ExtractTexture_FileMode: invalid fbxDir: \"%s\"", fbxDir.string().c_str());
                        return;
                    }
                    srcTex = fbxDir / srcTex;
                }

                // srcTex 정규화해봄 (.. 또는 . 제거)
                srcTex = srcTex.lexically_normal();

                if (!fs::exists(srcTex, ec))
                {
                    ALICE_LOG_WARN("[FbxImporter] ExtractTexture_FileMode: source texture does not exist: \"%s\"", srcTex.string().c_str());
                    return;
                }

                if (!fs::is_regular_file(srcTex, ec))
                {
                    ALICE_LOG_WARN("[FbxImporter] ExtractTexture_FileMode: source is not a regular file: \"%s\"", srcTex.string().c_str());
                    return;
                }

                fs::path dstTex = fbmDir / srcTex.filename();
                ec.clear();
                fs::copy_file(srcTex, dstTex, fs::copy_options::overwrite_existing, ec);
                if (!ec)
                {
                    extractedTextures.push_back(dstTex);
                }
                else
                {
                    ALICE_LOG_WARN("[FbxImporter] ExtractTexture_FileMode: failed to copy texture \"%s\" -> \"%s\": %s", 
                                   srcTex.string().c_str(), dstTex.string().c_str(), ec.message().c_str());
                }
            }
            catch (const std::exception& e)
            {
                ALICE_LOG_ERRORF("[FbxImporter] ExtractTexture_FileMode: exception while copying texture: %s", e.what());
            }
        }

        static void ExtractTexture_NoFileMode(ResourceManager& resources,
                                              const aiScene* scene,
                                              aiMaterial* mat,
                                              aiTextureType type,
                                              const char* tag,
                                              const std::filesystem::path& fbxPath,
                                              const std::string& baseName,
                                              std::vector<std::filesystem::path>& cookedTextures)
        {
            namespace fs = std::filesystem;

            if (!scene || !mat)
                return;

            aiString texPath;
            if (mat->GetTexture(type, 0, &texPath) != AI_SUCCESS)
                return;

            if (std::strlen(texPath.C_Str()) == 0)
                return;

            const std::string t = texPath.C_Str();

            const aiTexture* at = scene->GetEmbeddedTexture(t.c_str());
            if (at)
            {
                // 배포 모드에서는 평문 파일 생성 없이 메모리에서 바로 Cooked 저장
                if (at->mHeight != 0)
                    return;

                static int embeddedIndex = 0;
                const std::string texStem = baseName + "_" + tag + "_embedded" + std::to_string(embeddedIndex++);

                fs::path cooked = "Cooked/Textures";
                cooked /= baseName;
                cooked /= texStem + ".alice";
                const fs::path cookedAbs = resources.Resolve(cooked);

                std::vector<std::uint8_t> bytes;
                bytes.resize(at->mWidth);
                std::memcpy(bytes.data(), at->pcData, at->mWidth);
                resources.CookAndSaveBytes(bytes, cookedAbs);
                cookedTextures.push_back(cooked);
                return;
            }

            fs::path srcTex = t;
            if (!srcTex.is_absolute())
            {
                const std::string logicalFbx = NormalizeToResourceLogical(fbxPath);
                const fs::path fbxParent = fs::path(logicalFbx).parent_path();
                srcTex = fbxParent / srcTex;
            }

            auto texBytes = resources.LoadSharedBinaryAuto(srcTex);
            if (!texBytes || texBytes->empty())
                return;

            const std::string texStem = fs::path(t).stem().string();
            fs::path cooked = "Cooked/Textures";
            cooked /= baseName;
            cooked /= texStem + ".alice";
            const fs::path cookedAbs = resources.Resolve(cooked);
            resources.CookAndSaveBytes(*texBytes, cookedAbs);
            cookedTextures.push_back(cooked);
        }
    }

    FbxImporter::FbxImporter(ResourceManager& resources,
                             SkinnedMeshRegistry* meshRegistry)
        : m_resources(resources)
        , m_meshRegistry(meshRegistry)
    {
    }

    FbxImportResult FbxImporter::Import(ID3D11Device* device,
                                        const std::filesystem::path& fbxPath,
                                        const FbxImportOptions& /*options*/)
    {
        auto model = std::make_shared<FbxModel>();
        FbxImportResult result;

        ALICE_LOG_INFO("[FbxImporter] Import start: path=\"%s\"", fbxPath.string().c_str());

        // 절대 "decrypted 임시파일"을 만들지 않습니다.
        // - 파일이 있으면 그대로 파일 로드
        // - 없으면(Resource/Cooked/Chunks) 메모리에서 복호화된 바이트를 받아 Assimp ReadFileFromMemory 로 로드
        namespace fs = std::filesystem;

        const bool fileExists = !fbxPath.empty() && fs::exists(fbxPath);

        // 키/생성물 이름은 항상 원래 요청된 fbxPath 기준(stem)으로 고정함
        // C:/Models/Robot/robot_01.fbx -> 	robot_01
        std::string baseName = std::filesystem::path(fbxPath).stem().string();

        std::filesystem::path resolvedLogical = fbxPath;

        if (fileExists)
        {
            const fs::path absFbxPath = fs::absolute(fbxPath);
            if (!model->Load(device, absFbxPath.wstring()))
            {
                ALICE_LOG_ERRORF("[FbxImporter] FbxModel::Load FAILED for \"%s\"\n", absFbxPath.string().c_str());
                return result;
            }
        }
        else
        {
            auto sp = m_resources.LoadSharedBinaryAuto(resolvedLogical);
            if (!sp || sp->empty())
            {
                ALICE_LOG_ERRORF("[FbxImporter] Import FAILED: cooked load failed \"%s\"\n",
                    resolvedLogical.string().c_str());
                return result;
            }

            // baseDirW는 외부 텍스처 상대경로 해석용인데, 배포 빌드에선 파일이 없을 수 있어 빈 값으로 둡니다.
            if (!model->LoadFromMemory(device, sp->data(), sp->size(), baseName + ".fbx", L""))
            {
                ALICE_LOG_ERRORF("[FbxImporter] FbxModel::LoadFromMemory FAILED for \"%s\" (bytes=%zu)\n", resolvedLogical.string().c_str(), sp->size());
                return result;
            }
        }

        const aiScene* scene = model->GetScenePtr();
        if (!scene)
        {
            ALICE_LOG_ERRORF("[FbxImporter] model.GetScenePtr() returned null for \"%s\"\n",
                resolvedLogical.string().c_str());
            return result;
        }

        // 0-1) 스키닝 메시 GPU 를 레지스트리에 등록
        //     - FBX 모델이 유효하고 레지스트리가 주입된 경우에만 수행합니다.
        if (m_meshRegistry && model->HasMesh())
        {
            auto gpu = std::make_shared<SkinnedMeshGPU>();
            gpu->vertexBuffer = model->GetVertexBuffer(); // AddRef 발생
            gpu->indexBuffer  = model->GetIndexBuffer();
            gpu->stride       = model->GetVertexStride();
            gpu->indexCount   = static_cast<UINT>(model->GetIndexCount());
            gpu->startIndex   = 0;
            gpu->baseVertex   = 0;

            // 서브셋 / 머티리얼 SRV 복사
            gpu->subsets = model->GetSubsets();
            const auto& matSrvs = model->GetMaterialSRVs();
            const auto& nrmSrvs = model->GetNormalSRVs();
            gpu->materialSRVs.resize(matSrvs.size());
            gpu->normalSRVs.resize(nrmSrvs.size());
            gpu->materialOverridePaths.resize(matSrvs.size());
            for (std::size_t i = 0; i < matSrvs.size(); ++i)
            {
                gpu->materialSRVs[i] = matSrvs[i]; // ComPtr 으로 AddRef
                gpu->materialOverridePaths[i].clear();
            }
            for (std::size_t i = 0; i < nrmSrvs.size(); ++i)
            {
                gpu->normalSRVs[i] = nrmSrvs[i];
            }

            // 스켈레톤 정보 복사
            if (model->HasSkeleton())
            {
                gpu->skeleton     = model->GetSkeleton();
                gpu->skeletonRoot = model->GetSkeletonRoot();

                // 간단한 본 트리 텍스트 생성 (App.cpp 의 boneDisplayText 와 유사)
                const auto& nodes = gpu->skeleton;
                int root = gpu->skeletonRoot;

                std::string text;
                AppendSkeletonText(nodes, root, 0, text);
                gpu->skeletonText = text;
            }

            // 애니메이션 재생/클립 목록을 위해 원본 컨텍스트를 유지합니다.
            gpu->sourceModel = model;

            const std::string meshKey = baseName;
            m_meshRegistry->Register(meshKey, gpu);

            ALICE_LOG_INFO("[FbxImporter] Registered mesh key=\"%s\" stride=%u indexCount=%u subsets=%zu mats=%zu\n",
                meshKey.c_str(),
                gpu->stride,
                gpu->indexCount,
                gpu->subsets.size(),
                gpu->materialSRVs.size());
        }

        // 1) 텍스처는 "평문 파일로 추출"하지 않습니다.
        //    - 임베디드 텍스처: 메모리 바이트를 바로 Cooked/.../.alice 로 암호화 저장
        //    - 외부 텍스처: 원본 파일을 바로 읽어 Cooked 로 암호화 저장 (중간 파일 없음)

        std::vector<fs::path> cookedTextures;

        // (A) 에디터에서 FBX 파일을 직접 로드한 경우: 예전 방식대로 <fbx>.fbm 폴더를 만들고
        //     외부 텍스처는 거기에 복사, 임베디드 텍스처는 거기에 추출합니다.
        //     그런 다음 추출/복사한 파일들을 Cooked/Textures/.../.alice 로 암호화 저장합니다.
        if (fileExists)
        {
            const fs::path absFbxPath = fs::absolute(fbxPath);
            const fs::path fbxDir     = absFbxPath.parent_path();
            const fs::path fbmDir     = fbxDir / (baseName + ".fbm");
            {
                std::error_code ec;
                fs::create_directories(fbmDir, ec);
            }

            std::vector<fs::path> extractedTextures;

            for (unsigned mi = 0; mi < scene->mNumMaterials; ++mi)
            {
                aiMaterial* mat = scene->mMaterials[mi];
                ExtractTexture_FileMode(scene, mat, aiTextureType_BASE_COLOR, "Base", fbxDir, fbmDir, baseName, extractedTextures);
                ExtractTexture_FileMode(scene, mat, aiTextureType_DIFFUSE, "Diffuse", fbxDir, fbmDir, baseName, extractedTextures);
                ExtractTexture_FileMode(scene, mat, aiTextureType_NORMALS, "Normal", fbxDir, fbmDir, baseName, extractedTextures);
                ExtractTexture_FileMode(scene, mat, aiTextureType_METALNESS, "Metallic", fbxDir, fbmDir, baseName, extractedTextures);
                ExtractTexture_FileMode(scene, mat, aiTextureType_DIFFUSE_ROUGHNESS, "Roughness", fbxDir, fbmDir, baseName, extractedTextures);
            }

            // extractedTextures → Cooked/Textures/<fbxName>/<texStem>.alice
            for (const auto& texPath : extractedTextures)
            {
                fs::path cooked = "Cooked/Textures";
                cooked /= baseName;
                cooked /= texPath.stem().string() + ".alice";
                const fs::path cookedAbs = m_resources.Resolve(cooked);
                if (m_resources.CookAndSave(texPath, cookedAbs))
                    cookedTextures.push_back(cooked);
            }
        }
        // (B) 게임/배포 또는 원본 파일이 없는 경우: 기존 방식(메모리/원본 자동 로드 → Cooked 저장)
        else
        {
            for (unsigned mi = 0; mi < scene->mNumMaterials; ++mi)
            {
                aiMaterial* mat = scene->mMaterials[mi];
                ExtractTexture_NoFileMode(m_resources, scene, mat, aiTextureType_BASE_COLOR, "Base", fbxPath, baseName, cookedTextures);
                ExtractTexture_NoFileMode(m_resources, scene, mat, aiTextureType_DIFFUSE, "Diffuse", fbxPath, baseName, cookedTextures);
                ExtractTexture_NoFileMode(m_resources, scene, mat, aiTextureType_NORMALS, "Normal", fbxPath, baseName, cookedTextures);
                ExtractTexture_NoFileMode(m_resources, scene, mat, aiTextureType_METALNESS, "Metallic", fbxPath, baseName, cookedTextures);
                ExtractTexture_NoFileMode(m_resources, scene, mat, aiTextureType_DIFFUSE_ROUGHNESS, "Roughness", fbxPath, baseName, cookedTextures);
            }
        }

        // 4) 간단한 .mat 파일 생성
        //    - 현재는 추출된 텍스처 개수만큼 기본 머티리얼을 만들어 둡니다.
        for (std::size_t i = 0; i < cookedTextures.size(); ++i)
        {
            fs::path matDir  = m_resources.Resolve("Assets/Materials");
            std::error_code ec;
            fs::create_directories(matDir, ec);

            // 예: Hero_0.mat, Hero_1.mat ...
            fs::path matPath = matDir / (baseName + "_" + std::to_string(i) + ".mat");

            MaterialComponent matComp;
            matComp.color     = DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f);
            matComp.roughness = 0.5f;
            matComp.metalness = 0.0f;
            matComp.assetPath = matPath.string();
            if (i < cookedTextures.size())
            {
                matComp.albedoTexturePath = cookedTextures[i].string();
            }

            MaterialFile::Save(matPath, matComp);

            // 상대 경로로 변환하여 저장 D:\\Github\\AliceRenderer\\Assets\\Materials\\ -> (Assets/Materials/... 형식)
            fs::path matPathRelative = fs::path("Assets/Materials") / (baseName + "_" + std::to_string(i) + ".mat");
            result.materialAssetPaths.push_back(matPathRelative.generic_string());
        }

        // 5) 메시 자산의 논리 경로는 FBX 이름을 그대로 사용합니다.
        result.meshAssetPath = baseName;

        // 6) 에디터/World 에서 사용할 인스턴스 에셋(.fbxasset)을 생성합니다.
        //    - 언리얼의 SkeletalMesh 에셋 비슷한 개념으로, FBX 원본과 머티리얼을 묶어 둡니다.
        {
            fs::path fbxAssetDir = m_resources.Resolve("Assets/Fbx");
            std::error_code ec;
            fs::create_directories(fbxAssetDir, ec);

            fs::path fbxAssetPath = fbxAssetDir / (baseName + ".fbxasset");

            FbxInstanceAsset asset;
            asset.sourceFbx = NormalizeToResourceLogical(resolvedLogical);
            asset.meshAssetPath = result.meshAssetPath;
            asset.materialAssetPaths = result.materialAssetPaths;

            if (!SaveFbxInstanceAsset(fbxAssetPath, asset)) return result;

            // 상대 경로로 변환하여 저장 D:\\Github\\AliceRenderer\\Assets\\Materials -> (Assets/Fbx/... 형식)
            fs::path fbxAssetPathRelative = fs::path("Assets/Fbx") / (baseName + ".fbxasset");
            result.instanceAssetPath = fbxAssetPathRelative.generic_string();
        }

        // 디버그 로깅: Import 완료
        {

			ALICE_LOG_INFO("[FbxImporter] Import done: meshAssetPath=\"%s\", materials=%zu\n",
				result.meshAssetPath.c_str(),
				result.materialAssetPaths.size());
        }

        return result;
    }
}


