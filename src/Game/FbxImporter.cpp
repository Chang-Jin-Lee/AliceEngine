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
#include "3Dmodel/FbxModel.h"
#include "Rendering/SkinnedMeshRegistry.h"   // SkinnedMeshGPU / SkinnedMeshRegistry

namespace Alice
{
    namespace
    {
        // 간단한 이미지 확장자 체크 함수입니다.
        inline bool IsImageFile(const std::filesystem::path& path)
        {
            const std::string ext = path.extension().string();
            if (ext.empty()) return false;

            std::string lower = ext;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            return lower == ".png"  || lower == ".jpg"  || lower == ".jpeg" ||
                   lower == ".tga"  || lower == ".bmp"  || lower == ".dds";
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
        FbxImportResult result;

        // 디버그 로깅: Import 시작
        {
            char buf[256] = {};
            std::snprintf(buf, sizeof(buf),
                          "[FbxImporter] Import start: path=\"%s\"\n",
                          fbxPath.u8string().c_str());
            OutputDebugStringA(buf);
        }

        if (fbxPath.empty() || !std::filesystem::exists(fbxPath))
        {
            char buf[256] = {};
            std::snprintf(buf, sizeof(buf),
                          "[FbxImporter] Import FAILED: file not found \"%s\"\n",
                          fbxPath.u8string().c_str());
            OutputDebugStringA(buf);
            return result;
        }

        namespace fs = std::filesystem;

        const fs::path absFbxPath = fs::absolute(fbxPath);
        const fs::path fbxDir     = absFbxPath.parent_path();
        const std::string baseName = absFbxPath.stem().string();

        // 0) FbxModel 로 FBX 읽기 (D3D11-AliceTutorial Common/Mesh 포팅 버전)
        FbxModel model;
        if (!model.Load(device, absFbxPath.wstring()))
        {
            char buf[256] = {};
            std::snprintf(buf, sizeof(buf),
                          "[FbxImporter] FbxModel::Load FAILED for \"%s\"\n",
                          absFbxPath.u8string().c_str());
            OutputDebugStringA(buf);
            return result;
        }

        const aiScene* scene = model.GetScenePtr();
        if (!scene)
        {
            char buf[256] = {};
            std::snprintf(buf, sizeof(buf),
                          "[FbxImporter] model.GetScenePtr() returned null for \"%s\"\n",
                          absFbxPath.u8string().c_str());
            OutputDebugStringA(buf);
            return result;
        }

        // 0-1) 스키닝 메시 GPU 를 레지스트리에 등록 (선택적)
        //      - FBX 모델이 유효하고 레지스트리가 주입된 경우에만 수행합니다.
        if (m_meshRegistry && model.HasMesh())
        {
            auto gpu = std::make_shared<SkinnedMeshGPU>();
            gpu->vertexBuffer = model.GetVertexBuffer(); // AddRef 발생
            gpu->indexBuffer  = model.GetIndexBuffer();
            gpu->stride       = model.GetVertexStride();
            gpu->indexCount   = static_cast<UINT>(model.GetIndexCount());
            gpu->startIndex   = 0;
            gpu->baseVertex   = 0;

            // 서브셋 / 머티리얼 SRV 복사
            gpu->subsets = model.GetSubsets();
            const auto& matSrvs = model.GetMaterialSRVs();
            gpu->materialSRVs.resize(matSrvs.size());
            gpu->materialOverridePaths.resize(matSrvs.size());
            for (std::size_t i = 0; i < matSrvs.size(); ++i)
            {
                gpu->materialSRVs[i] = matSrvs[i]; // ComPtr 으로 AddRef
                gpu->materialOverridePaths[i].clear();
            }

            // 스켈레톤 정보 복사
            if (model.HasSkeleton())
            {
                gpu->skeleton     = model.GetSkeleton();
                gpu->skeletonRoot = model.GetSkeletonRoot();

                // 간단한 본 트리 텍스트 생성 (App.cpp 의 boneDisplayText 와 유사)
                const auto& nodes = gpu->skeleton;
                int root = gpu->skeletonRoot;

                std::string text;
                std::function<void(int,int)> dfs = [&](int idx, int depth)
                {
                    if (idx < 0 || idx >= (int)nodes.size()) return;
                    const auto& n = nodes[(std::size_t)idx];
                    text.append(depth * 2, ' ');
                    text += n.name;
                    text += "\n";
                    for (int child : n.children)
                    {
                        dfs(child, depth + 1);
                    }
                };
                dfs(root, 0);
                gpu->skeletonText = text;
            }

            const std::string meshKey = baseName;
            m_meshRegistry->Register(meshKey, gpu);

            char buf[256] = {};
            std::snprintf(buf, sizeof(buf),
                          "[FbxImporter] Registered mesh key=\"%s\" stride=%u indexCount=%u subsets=%zu mats=%zu\n",
                          meshKey.c_str(),
                          gpu->stride,
                          gpu->indexCount,
                          gpu->subsets.size(),
                          gpu->materialSRVs.size());
            OutputDebugStringA(buf);
        }

        // 1) .fbm 디렉터리 생성 (언리얼/튜토리얼과 같은 규칙)
        fs::path fbmDir = fbxDir / (baseName + ".fbm");
        {
            std::error_code ec;
            fs::create_directories(fbmDir, ec);
        }

        // 2) FBX 머티리얼에서 텍스처 경로/임베디드 텍스처를 추출해서 .fbm 아래에 저장합니다.
        //    - BaseColor/Diffuse, Normal, Roughness, Metallic 정도만 처리합니다.
        std::vector<fs::path> extractedTextures;

        auto extractTextureFromMaterial = [&](aiMaterial* mat, aiTextureType type, const char* tag)
        {
            if (!mat) return;

            aiString texPath;
            if (mat->GetTexture(type, 0, &texPath) != AI_SUCCESS)
                return;

            if (std::strlen(texPath.C_Str()) == 0)
                return;

            const std::string t = texPath.C_Str();

            // 2-1) 임베디드 텍스처인지 확인
            const aiTexture* at = scene->GetEmbeddedTexture(t.c_str());
            if (at)
            {
                // 임베디드 텍스처는 가능한 원본 이미지 형식(PNG/JPG/TGA 등)으로 저장합니다.
                // - mHeight == 0 인 경우, mWidth 바이트의 압축 이미지 데이터(예: PNG)를 achFormatHint 기반 확장자로 저장.
                // - mHeight > 0 인 경우, BGRA raw 이므로 간단히 .dds 로 저장해 둡니다.
                static int embeddedIndex = 0;

                std::string ext = at->achFormatHint;
                if (ext.empty())
                    ext = (at->mHeight == 0) ? "bin" : "dds";

                fs::path outPath = fbmDir / (baseName + "_" + tag + "_embedded" + std::to_string(embeddedIndex++) + "." + ext);

                std::ofstream ofs(outPath, std::ios::binary);
                if (ofs.is_open())
                {
                    if (at->mHeight == 0)
                    {
                        // 압축된 이미지 데이터 (PNG/JPG 등)
                        ofs.write(reinterpret_cast<const char*>(at->pcData), at->mWidth);
                    }
                    else
                    {
                        // raw BGRA 데이터 (간단히 그대로 저장, 확장자는 .dds 로 표시)
                        ofs.write(reinterpret_cast<const char*>(at->pcData),
                                  at->mWidth * at->mHeight * sizeof(aiTexel));
                    }
                    extractedTextures.push_back(outPath);
                }
                return;
            }

            // 2-2) 외부 파일 텍스처일 경우, 실제 파일을 .fbm 으로 복사
            fs::path srcTex = t;
            // 절대 경로가 아니면 FBX 폴더 기준 상대 경로로 해석합니다.
            if (!srcTex.is_absolute())
            {
                srcTex = fbxDir / srcTex;
            }

            if (!fs::exists(srcTex))
                return;

            fs::path dstTex = fbmDir / srcTex.filename();

            std::error_code ec;
            fs::copy_file(srcTex, dstTex, fs::copy_options::overwrite_existing, ec);
            if (!ec)
            {
                extractedTextures.push_back(dstTex);
            }
        };

        for (unsigned mi = 0; mi < scene->mNumMaterials; ++mi)
        {
            aiMaterial* mat = scene->mMaterials[mi];
            extractTextureFromMaterial(mat, aiTextureType_BASE_COLOR, "Base");
            extractTextureFromMaterial(mat, aiTextureType_DIFFUSE,    "Diffuse");
            extractTextureFromMaterial(mat, aiTextureType_NORMALS,    "Normal");
            extractTextureFromMaterial(mat, aiTextureType_METALNESS,  "Metallic");
            extractTextureFromMaterial(mat, aiTextureType_DIFFUSE_ROUGHNESS, "Roughness");
        }

        // 3) 추출/복사한 텍스처들에 대해 암호화된 .alice 를 생성합니다.
        //    - 예: ../Cooked/Textures/<fbxName>/<원본이름>.alice
        std::vector<fs::path> cookedTextures;
        for (const auto& texPath : extractedTextures)
        {
            fs::path cooked = "../Cooked/Textures";
            cooked /= baseName;
            cooked /= texPath.stem().string() + ".alice";

            m_resources.CookAndSave(texPath, cooked);
            cookedTextures.push_back(cooked);
        }

        // 4) 간단한 .mat 파일 생성
        //    - 현재는 추출된 텍스처 개수만큼 기본 머티리얼을 만들어 둡니다.
        for (std::size_t i = 0; i < cookedTextures.size(); ++i)
        {
            fs::path matDir  = "../Assets/Materials";
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

            result.materialAssetPaths.push_back(matPath.string());
        }

        // 5) 메시 자산의 논리 경로는 FBX 이름을 그대로 사용합니다.
        result.meshAssetPath = baseName;

        // 6) 에디터/World 에서 사용할 인스턴스 에셋(.fbxasset)을 생성합니다.
        //    - 언리얼의 SkeletalMesh 에셋 비슷한 개념으로, FBX 원본과 머티리얼을 묶어 둡니다.
        {
            fs::path fbxAssetDir = "../Assets/Fbx";
            std::error_code ec;
            fs::create_directories(fbxAssetDir, ec);

            fs::path fbxAssetPath = fbxAssetDir / (baseName + ".fbxasset");

            std::ofstream ofs(fbxAssetPath);
            if (ofs.is_open())
            {
                ofs << "source_fbx=" << absFbxPath.string() << "\n";
                ofs << "mesh=" << result.meshAssetPath << "\n";
                for (const auto& matPath : result.materialAssetPaths)
                {
                    ofs << "mat=" << matPath << "\n";
                }
                ofs.close();
                result.instanceAssetPath = fbxAssetPath.string();
            }
        }

        // 디버그 로깅: Import 완료
        {
            char buf[256] = {};
            std::snprintf(buf, sizeof(buf),
                          "[FbxImporter] Import done: meshAssetPath=\"%s\", materials=%zu\n",
                          result.meshAssetPath.c_str(),
                          result.materialAssetPaths.size());
            OutputDebugStringA(buf);
        }

        return result;
    }
}


