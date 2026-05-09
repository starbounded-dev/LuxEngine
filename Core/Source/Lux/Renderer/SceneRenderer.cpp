#include "lpch.h"
#include "SceneRenderer.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Core/Application.h"
#include "Lux/Asset/AssetManager.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <limits>

namespace Lux {

	// Push-constant layout that every mesh-draw shader in this engine expects.
	// Must match the push_constant block declared in HazelPBR_Static, PreDepth,
	// DirShadowMap, SelectedGeometry, and Wireframe shaders.
	struct MeshDrawPushConstants
	{
		uint32_t ObjectIndexBase = 0; // first index into ObjectIndexes SSBO
		uint32_t LightIndex = 0; // shadow cascade index (0 = single cascade)
		uint32_t BoneTransformBase = 0; // unused (no animation)
		uint32_t BoneTransformStride = 0; // unused
	};

	namespace
	{
		Ref<TextureCube> GetEnvironmentRadianceMap(const Ref<Environment>& environment)
		{
			return environment && environment->RadianceMap ? environment->RadianceMap : Renderer::GetBlackCubeTexture();
		}

		Ref<TextureCube> GetEnvironmentIrradianceMap(const Ref<Environment>& environment)
		{
			return environment && environment->IrradianceMap ? environment->IrradianceMap : Renderer::GetBlackCubeTexture();
		}

		AssetHandle GetStaticMeshKeyHandle(const Ref<StaticMesh>& staticMesh)
		{
			if (!staticMesh)
				return 0;

			return staticMesh->Handle ? staticMesh->Handle : staticMesh->GetMeshSource();
		}

		uint32_t AlignUp(uint32_t value, uint32_t alignment)
		{
			if (alignment == 0)
				return value;

			return ((value + alignment - 1u) / alignment) * alignment;
		}

		uint32_t DivideRoundUp(uint32_t value, uint32_t divisor)
		{
			return divisor == 0 ? value : (value + divisor - 1u) / divisor;
		}

		glm::uvec2 DivideRoundUp(const glm::uvec2& value, uint32_t divisor)
		{
			return { DivideRoundUp(value.x, divisor), DivideRoundUp(value.y, divisor) };
		}

		uint32_t NextPowerOfTwo(uint32_t value)
		{
			if (value <= 1)
				return 1;

			value--;
			value |= value >> 1;
			value |= value >> 2;
			value |= value >> 4;
			value |= value >> 8;
			value |= value >> 16;
			return value + 1;
		}

		AssetHandle ResolveStaticMeshMaterialHandle(
			const Ref<MaterialTable>& materialTable,
			const Ref<StaticMesh>& staticMesh,
			const Ref<MeshSource>& meshSource,
			uint32_t materialIndex)
		{
			if (materialTable)
			{
				if (materialTable->HasMaterial(materialIndex))
					return materialTable->GetMaterial(materialIndex);

				const auto& overrides = materialTable->GetMaterials();
				if (overrides.size() == 1 && materialTable->HasMaterial(0))
					return materialTable->GetMaterial(0);
			}

			Ref<MaterialTable> staticMeshMaterials = staticMesh ? staticMesh->GetMaterials() : nullptr;
			if (staticMeshMaterials && staticMeshMaterials->HasMaterial(materialIndex))
				return staticMeshMaterials->GetMaterial(materialIndex);

			if (meshSource && materialIndex < meshSource->GetMaterials().size())
				return meshSource->GetMaterials()[materialIndex];

			return 0;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Construction / destruction
	// ─────────────────────────────────────────────────────────────────────────

	SceneRenderer::SceneRenderer(Ref<Scene> scene, SceneRendererSpecification specification)
		: m_Scene(scene), m_Specification(specification)
	{
		Init();
	}

	SceneRenderer::~SceneRenderer()
	{
		Shutdown();
	}

	void SceneRenderer::Shutdown()
	{
		// Ref<> handles GPU resource lifetimes automatically.
	}

	void SceneRenderer::SetScene(Ref<Scene> scene)
	{
		LUX_CORE_ASSERT(!m_Active, "Cannot change scene while rendering");
		m_Scene = scene;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Init
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::Init()
	{
		m_CommandBuffer = RenderCommandBuffer::Create(0, "SceneRenderer",       /*queries=*/true);
		m_UploadCommandBuffer = RenderCommandBuffer::Create(0, "SceneRenderer-Upload", /*queries=*/false);

		m_Renderer2D = Ref<Renderer2D>::Create(Renderer2DSpecification{});
		m_DebugRenderer = Ref<DebugRenderer>::Create();

		// Use window size if none specified
		if (m_Specification.ViewportWidth == 0) m_Specification.ViewportWidth = Application::Get().GetWindow().GetWidth();
		if (m_Specification.ViewportHeight == 0) m_Specification.ViewportHeight = Application::Get().GetWindow().GetHeight();
		m_ViewportWidth = m_Specification.ViewportWidth;
		m_ViewportHeight = m_Specification.ViewportHeight;
		m_InvViewportWidth = m_ViewportWidth > 0 ? 1.0f / (float)m_ViewportWidth : 0.0f;
		m_InvViewportHeight = m_ViewportHeight > 0 ? 1.0f / (float)m_ViewportHeight : 0.0f;

		// ── Uniform buffer sets ───────────────────────────────────────────────
		m_UBSCamera = UniformBufferSet::Create(sizeof(UBCamera));
		m_UBSScene = UniformBufferSet::Create(sizeof(UBScene));
		m_UBSShadow = UniformBufferSet::Create(sizeof(UBShadow));
		m_UBSRendererData = UniformBufferSet::Create(sizeof(UBRendererData));
		m_UBSPointLights = UniformBufferSet::Create(sizeof(UBPointLights));
		m_UBSSpotLights = UniformBufferSet::Create(sizeof(UBSpotLights));
		m_UBSSpotShadow = UniformBufferSet::Create(sizeof(UBSpotShadow));
		m_UBSScreenData = UniformBufferSet::Create(sizeof(UBScreenData));

		// ── Storage buffer sets (start with generous initial capacity) ─────────
		{
			StorageBufferSpecification spec;
			spec.GPUOnly = false;
			spec.DebugName = "InstanceTransforms";
			// Pre-allocate 4096 transform slots  (each is 48 bytes)
			m_SBSInstanceTransforms = StorageBufferSet::Create(spec, sizeof(TransformVertexData) * 4096);
		}
		{
			StorageBufferSpecification spec;
			spec.GPUOnly = false;
			spec.DebugName = "ObjectIndexes";
			m_SBSObjectIndexes = StorageBufferSet::Create(spec, sizeof(uint32_t) * 4096);
		}
		{
			StorageBufferSpecification indexSpec;
			indexSpec.GPUOnly = true;
			indexSpec.DebugName = "VisiblePointLightIndices";
			m_SBSVisiblePointLightIndices = StorageBufferSet::Create(indexSpec, sizeof(int32_t) * MaxVisibleLightsPerTile);

			indexSpec.DebugName = "VisibleSpotLightIndices";
			m_SBSVisibleSpotLightIndices = StorageBufferSet::Create(indexSpec, sizeof(int32_t) * MaxVisibleLightsPerTile);
			ResizeLightCullingResources();
		}

		// Common vertex layout for all opaque mesh pipelines
		VertexBufferLayout vertexLayout = {
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float3, "a_Normal"   },
			{ ShaderDataType::Float3, "a_Tangent"  },
			{ ShaderDataType::Float3, "a_Binormal" },
			{ ShaderDataType::Float2, "a_TexCoord" }
		};

		// ── Shadow map (single directional, ortho projection) ─────────────────
		{
			ImageSpecification shadowMapSpec;
			shadowMapSpec.DebugName = "ShadowMapArray";
			shadowMapSpec.Dimension = nvrhi::TextureDimension::Texture2DArray;
			shadowMapSpec.Format = ImageFormat::Depth;
			shadowMapSpec.Usage = ImageUsage::Attachment;
			shadowMapSpec.Width = 4096;
			shadowMapSpec.Height = 4096;
			shadowMapSpec.Layers = 4;
			m_ShadowMapImage = Image2D::Create(shadowMapSpec);
			m_ShadowMapImage->RT_Invalidate();

			FramebufferSpecification fbSpec;
			fbSpec.Width = 4096;
			fbSpec.Height = 4096;
			fbSpec.Attachments = { ImageFormat::Depth };
			fbSpec.DepthClearValue = 1.0f;
			fbSpec.DebugName = "ShadowMap";
			fbSpec.ExistingImage = m_ShadowMapImage;
			fbSpec.ExistingImageLayer = 0;

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "DirShadowMap";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("DirShadowMap");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;
			pipelineSpec.BackfaceCulling = false; // avoid peter-panning

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "ShadowMapPass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);

			m_ShadowMapPass = RenderPass::Create(rpSpec);
			m_ShadowMapPass->SetInput("ShadowData", m_UBSShadow);
			m_ShadowMapPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_ShadowMapPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_ShadowMapPass->Validate());
			m_ShadowMapPass->Bake();

			m_ShadowPassMaterial = Material::Create(pipelineSpec.Shader, "ShadowPass");
		}

		// ── Spot shadow atlas (single depth atlas) ───────────────────────────
		{
			m_SpotShadowMapSize = 2048;
			m_SpotShadowAtlasGridSize = 1;
			m_SpotShadowTileSize = m_SpotShadowMapSize;

			ImageSpecification spotShadowSpec;
			spotShadowSpec.DebugName = "SpotShadowAtlas";
			spotShadowSpec.Dimension = nvrhi::TextureDimension::Texture2D;
			spotShadowSpec.Format = ImageFormat::Depth;
			spotShadowSpec.Usage = ImageUsage::Attachment;
			spotShadowSpec.Width = m_SpotShadowMapSize;
			spotShadowSpec.Height = m_SpotShadowMapSize;
			m_SpotShadowMapImage = Image2D::Create(spotShadowSpec);
			m_SpotShadowMapImage->RT_Invalidate();

			FramebufferSpecification fbSpec;
			fbSpec.Width = m_SpotShadowMapSize;
			fbSpec.Height = m_SpotShadowMapSize;
			fbSpec.Attachments = { ImageFormat::Depth };
			fbSpec.DepthClearValue = 1.0f;
			fbSpec.DebugName = "SpotShadowAtlas";
			fbSpec.ExistingImage = m_SpotShadowMapImage;

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "SpotShadowMap";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("SpotShadowMap");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::LessOrEqual;
			pipelineSpec.BackfaceCulling = false;

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "SpotShadowMapPass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);

			m_SpotShadowMapPass = RenderPass::Create(rpSpec);
			m_SpotShadowMapPass->SetInput("SpotShadowData", m_UBSSpotShadow);
			m_SpotShadowMapPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_SpotShadowMapPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_SpotShadowMapPass->Validate());
			m_SpotShadowMapPass->Bake();

			m_SpotShadowPassMaterial = Material::Create(pipelineSpec.Shader, "SpotShadowPass");
		}

		// ── Pre-depth pass ────────────────────────────────────────────────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { ImageFormat::DEPTH32FSTENCIL8UINT };
			fbSpec.DepthClearValue = 0.0f;
			fbSpec.ClearDepthOnLoad = true;
			fbSpec.DebugName = "PreDepth";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "PreDepth";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("PreDepth");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::GreaterOrEqual;

			m_PreDepthPipeline = Pipeline::Create(pipelineSpec);
			m_PreDepthMaterial = Material::Create(pipelineSpec.Shader, "PreDepth");

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "PreDepthPass";
			rpSpec.Pipeline = m_PreDepthPipeline;

			m_PreDepthPass = RenderPass::Create(rpSpec);
			m_PreDepthPass->SetInput("Camera", m_UBSCamera);
			m_PreDepthPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_PreDepthPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_PreDepthPass->Validate());
			m_PreDepthPass->Bake();
		}

		// ── Hierarchical depth + SSR pre-integration ──────────────────────────
		{
			TextureSpecification hzbSpec;
			hzbSpec.Format = ImageFormat::RED32F;
			hzbSpec.Width = 1;
			hzbSpec.Height = 1;
			hzbSpec.SamplerWrap = TextureWrap::Clamp;
			hzbSpec.SamplerFilter = TextureFilter::Nearest;
			hzbSpec.Storage = true;
			hzbSpec.GenerateMips = true;
			hzbSpec.DebugName = "HierarchicalZ";
			m_HierarchicalDepthTexture.Texture = Texture2D::Create(hzbSpec);

			ComputePassSpecification hzbPassSpec;
			hzbPassSpec.DebugName = "HierarchicalDepth";
			hzbPassSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("HZB"));
			m_HierarchicalDepthPass = ComputePass::Create(hzbPassSpec);
			m_HierarchicalDepthPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_HierarchicalDepthPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_HierarchicalDepthPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_HierarchicalDepthPass->Validate());
			m_HierarchicalDepthPass->Bake();

			TextureSpecification visibilitySpec;
			visibilitySpec.Format = ImageFormat::RED8UN;
			visibilitySpec.Width = 1;
			visibilitySpec.Height = 1;
			visibilitySpec.SamplerWrap = TextureWrap::Clamp;
			visibilitySpec.SamplerFilter = TextureFilter::Linear;
			visibilitySpec.Storage = true;
			visibilitySpec.GenerateMips = true;
			visibilitySpec.DebugName = "Pre-Integration";
			m_PreIntegrationVisibilityTexture.Texture = Texture2D::Create(visibilitySpec);

			ComputePassSpecification preIntegrationSpec;
			preIntegrationSpec.DebugName = "Pre-Integration";
			preIntegrationSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("Pre-Integration"));
			m_PreIntegrationPass = ComputePass::Create(preIntegrationSpec);
			m_PreIntegrationPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_PreIntegrationPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_PreIntegrationPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_PreIntegrationPass->Validate());
			m_PreIntegrationPass->Bake();

			TextureSpecification preConvolutionSpec;
			preConvolutionSpec.Format = ImageFormat::RGBA32F;
			preConvolutionSpec.Width = 1;
			preConvolutionSpec.Height = 1;
			preConvolutionSpec.SamplerWrap = TextureWrap::Clamp;
			preConvolutionSpec.Storage = true;
			preConvolutionSpec.GenerateMips = true;
			preConvolutionSpec.DebugName = "Pre-Convoluted";
			m_PreConvolutedTexture.Texture = Texture2D::Create(preConvolutionSpec);

			ComputePassSpecification preConvolutionPassSpec;
			preConvolutionPassSpec.DebugName = "Pre-Convolution";
			preConvolutionPassSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("Pre-Convolution"));
			m_PreConvolutionComputePass = ComputePass::Create(preConvolutionPassSpec);
			m_PreConvolutionComputePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_PreConvolutionComputePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_PreConvolutionComputePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_PreConvolutionComputePass->Validate());
			m_PreConvolutionComputePass->Bake();
		}

		// ── Tiled light culling pass ─────────────────────────────────────────
		{
			ComputePassSpecification computeSpec;
			computeSpec.DebugName = "LightCulling";
			computeSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("LightCulling"));

			m_LightCullingPass = ComputePass::Create(computeSpec);
			m_LightCullingPass->SetInput("u_DepthMap", m_PreDepthPass->GetDepthOutput());
			m_LightCullingPass->SetInput("ShadowData", m_UBSShadow);
			m_LightCullingPass->SetInput("SceneData", m_UBSScene);
			m_LightCullingPass->SetInput("PointLightData", m_UBSPointLights);
			m_LightCullingPass->SetInput("SpotLightData", m_UBSSpotLights);
			m_LightCullingPass->SetInput("SpotShadowData", m_UBSSpotShadow);
			m_LightCullingPass->SetInput("VisiblePointLightIndicesBuffer", m_SBSVisiblePointLightIndices);
			m_LightCullingPass->SetInput("VisibleSpotLightIndicesBuffer", m_SBSVisibleSpotLightIndices);
			m_LightCullingPass->SetInput("Camera", m_UBSCamera);
			m_LightCullingPass->SetInput("RendererData", m_UBSRendererData);
			m_LightCullingPass->SetInput("ScreenData", m_UBSScreenData);
			m_LightCullingPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_LightCullingPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_LightCullingPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_LightCullingPass->Validate());
			m_LightCullingPass->Bake();
		}

		// ── Geometry framebuffer (owns the color + normal + material images) ──
		{
			FramebufferSpecification geoFBSpec;
			geoFBSpec.Width = m_ViewportWidth;
			geoFBSpec.Height = m_ViewportHeight;
			// color | view-space normals | metalness+roughness | depth (shared with pre-depth)
			geoFBSpec.Attachments = {
				ImageFormat::RGBA32F,
				ImageFormat::RGBA16F,
				ImageFormat::RGBA,
				ImageFormat::DEPTH32FSTENCIL8UINT
			};
			geoFBSpec.ExistingImages[3] = m_PreDepthPass->GetDepthOutput();
			geoFBSpec.Attachments.Attachments[0].LoadOp = AttachmentLoadOp::Load; // skybox writes first
			geoFBSpec.Attachments.Attachments[1].Blend = false;
			geoFBSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			geoFBSpec.ClearDepthOnLoad = false;
			geoFBSpec.DebugName = "Geometry";
			m_GeometryPassFramebuffer = Framebuffer::Create(geoFBSpec);

			// "Load" variant: subsequent draws share the same images without clearing.
			geoFBSpec.ClearColorOnLoad = false;
			geoFBSpec.ExistingImages[0] = m_GeometryPassFramebuffer->GetImage(0);
			geoFBSpec.ExistingImages[1] = m_GeometryPassFramebuffer->GetImage(1);
			geoFBSpec.ExistingImages[2] = m_GeometryPassFramebuffer->GetImage(2);
			Ref<Framebuffer> loadFB = Framebuffer::Create(geoFBSpec);

			// Opaque PBR pipeline
			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "PBR-Static";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("LuxPBR_Static");
			pipelineSpec.TargetFramebuffer = loadFB;
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::Equal; // rely on pre-depth
			pipelineSpec.DepthWrite = false;
			m_GeometryPipeline = Pipeline::Create(pipelineSpec);

			// Transparent PBR pipeline (alpha-blend, depth-test but no pre-depth Equal trick)
			pipelineSpec.DebugName = "PBR-Transparent";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("LuxPBR_Transparent");
			pipelineSpec.DepthOperator = DepthCompareOperator::GreaterOrEqual;
			pipelineSpec.DepthWrite = false;
			m_TransparentGeometryPipeline = Pipeline::Create(pipelineSpec);
		}

		// ── Geometry render passes ────────────────────────────────────────────
		{
			// Opaque pass
			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "GeometryPass";
			rpSpec.Pipeline = m_GeometryPipeline;
			m_GeometryPass = RenderPass::Create(rpSpec);

			m_GeometryPass->SetInput("Camera", m_UBSCamera);
			m_GeometryPass->SetInput("SceneData", m_UBSScene);
			m_GeometryPass->SetInput("ShadowData", m_UBSShadow);
			m_GeometryPass->SetInput("RendererData", m_UBSRendererData);
			m_GeometryPass->SetInput("PointLightData", m_UBSPointLights);
			m_GeometryPass->SetInput("SpotLightData", m_UBSSpotLights);
			m_GeometryPass->SetInput("SpotShadowData", m_UBSSpotShadow);
			m_GeometryPass->SetInput("VisiblePointLightIndicesBuffer", m_SBSVisiblePointLightIndices);
			m_GeometryPass->SetInput("VisibleSpotLightIndicesBuffer", m_SBSVisibleSpotLightIndices);
			m_GeometryPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_GeometryPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			// Environment textures – overridden each frame in BeginScene once env is set
			m_GeometryPass->SetInput("u_EnvRadianceTex", Renderer::GetBlackCubeTexture());
			m_GeometryPass->SetInput("u_EnvIrradianceTex", Renderer::GetBlackCubeTexture());
			m_GeometryPass->SetInput("u_BRDFLUTTexture", Renderer::GetBRDFLutTexture());
			// Shadow map output from the shadow pass above
			m_GeometryPass->SetInput("u_ShadowMapTexture", m_ShadowMapPass->GetDepthOutput());
			m_GeometryPass->SetInput("u_SpotShadowTexture", m_SpotShadowMapImage);
			LUX_CORE_VERIFY(m_GeometryPass->Validate());
			m_GeometryPass->Bake();

			// Transparent pass (same bindings, different pipeline)
			rpSpec.DebugName = "GeometryPass-Transparent";
			rpSpec.Pipeline = m_TransparentGeometryPipeline;
			m_GeometryPassTransparent = RenderPass::Create(rpSpec);

			m_GeometryPassTransparent->SetInput("Camera", m_UBSCamera);
			m_GeometryPassTransparent->SetInput("SceneData", m_UBSScene);
			m_GeometryPassTransparent->SetInput("ShadowData", m_UBSShadow);
			m_GeometryPassTransparent->SetInput("RendererData", m_UBSRendererData);
			m_GeometryPassTransparent->SetInput("PointLightData", m_UBSPointLights);
			m_GeometryPassTransparent->SetInput("SpotLightData", m_UBSSpotLights);
			m_GeometryPassTransparent->SetInput("SpotShadowData", m_UBSSpotShadow);
			m_GeometryPassTransparent->SetInput("VisiblePointLightIndicesBuffer", m_SBSVisiblePointLightIndices);
			m_GeometryPassTransparent->SetInput("VisibleSpotLightIndicesBuffer", m_SBSVisibleSpotLightIndices);
			m_GeometryPassTransparent->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_GeometryPassTransparent->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			// Environment textures – overridden each frame in BeginScene once env is set
			m_GeometryPassTransparent->SetInput("u_EnvRadianceTex", Renderer::GetBlackCubeTexture());
			m_GeometryPassTransparent->SetInput("u_EnvIrradianceTex", Renderer::GetBlackCubeTexture());
			m_GeometryPassTransparent->SetInput("u_BRDFLUTTexture", Renderer::GetBRDFLutTexture());
			// Shadow map output from the shadow pass above
			m_GeometryPassTransparent->SetInput("u_ShadowMapTexture", m_ShadowMapPass->GetDepthOutput());
			m_GeometryPassTransparent->SetInput("u_SpotShadowTexture", m_SpotShadowMapImage);
			LUX_CORE_VERIFY(m_GeometryPassTransparent->Validate());
			m_GeometryPassTransparent->Bake();
		}

		// ── GTAO + AO composite ───────────────────────────────────────────────
		{
			ImageSpecification imageSpec;
			imageSpec.Format = ImageFormat::RED8UI;
			imageSpec.Usage = ImageUsage::Storage;
			imageSpec.DebugName = "GTAO";
			m_GTAOOutputImage = Image2D::Create(imageSpec);

			imageSpec.DebugName = "GTAO-Denoise";
			m_GTAODenoiseImage = Image2D::Create(imageSpec);

			imageSpec.Format = ImageFormat::RED8UN;
			imageSpec.DebugName = "GTAO-Edges";
			m_GTAOEdgesOutputImage = Image2D::Create(imageSpec);

			Ref<Shader> gtaoShader = Renderer::GetShaderLibrary()->Get("GTAO");
			ComputePassSpecification gtaoSpec;
			gtaoSpec.DebugName = "GTAO-ComputePass";
			gtaoSpec.Pipeline = PipelineCompute::Create(gtaoShader);
			m_GTAOComputePass = ComputePass::Create(gtaoSpec);
			m_GTAOComputePass->SetInput("u_HiZDepth", m_HierarchicalDepthTexture.Texture);
			m_GTAOComputePass->SetInput("u_HilbertLut", Renderer::GetHilbertLut());
			m_GTAOComputePass->SetInput("u_ViewNormal", m_GeometryPass->GetOutput(1));
			m_GTAOComputePass->SetInput("o_AOwBentNormals", m_GTAOOutputImage);
			m_GTAOComputePass->SetInput("o_Edges", m_GTAOEdgesOutputImage);
			m_GTAOComputePass->SetInput("u_samplerPointClamp", Renderer::GetPointSampler());
			m_GTAOComputePass->SetInput("Camera", m_UBSCamera);
			m_GTAOComputePass->SetInput("ScreenData", m_UBSScreenData);
			LUX_CORE_VERIFY(m_GTAOComputePass->Validate());
			m_GTAOComputePass->Bake();

			Ref<Shader> denoiseShader = Renderer::GetShaderLibrary()->Get("GTAO-Denoise");
			m_GTAODenoiseMaterial[0] = Material::Create(denoiseShader, "GTAO-Denoise-Ping");
			m_GTAODenoiseMaterial[1] = Material::Create(denoiseShader, "GTAO-Denoise-Pong");

			ComputePassSpecification denoiseSpec;
			denoiseSpec.DebugName = "GTAO-Denoise";
			denoiseSpec.Pipeline = PipelineCompute::Create(denoiseShader);
			m_GTAODenoisePass[0] = ComputePass::Create(denoiseSpec);
			m_GTAODenoisePass[0]->SetInput("u_Edges", m_GTAOEdgesOutputImage);
			m_GTAODenoisePass[0]->SetInput("u_AOTerm", m_GTAOOutputImage);
			m_GTAODenoisePass[0]->SetInput("o_AOTerm", m_GTAODenoiseImage);
			m_GTAODenoisePass[0]->SetInput("ScreenData", m_UBSScreenData);
			m_GTAODenoisePass[0]->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_GTAODenoisePass[0]->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_GTAODenoisePass[0]->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_GTAODenoisePass[0]->Validate());
			m_GTAODenoisePass[0]->Bake();

			m_GTAODenoisePass[1] = ComputePass::Create(denoiseSpec);
			m_GTAODenoisePass[1]->SetInput("u_Edges", m_GTAOEdgesOutputImage);
			m_GTAODenoisePass[1]->SetInput("u_AOTerm", m_GTAODenoiseImage);
			m_GTAODenoisePass[1]->SetInput("o_AOTerm", m_GTAOOutputImage);
			m_GTAODenoisePass[1]->SetInput("ScreenData", m_UBSScreenData);
			m_GTAODenoisePass[1]->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_GTAODenoisePass[1]->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_GTAODenoisePass[1]->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_GTAODenoisePass[1]->Validate());
			m_GTAODenoisePass[1]->Bake();

			m_GTAOFinalImage = (m_Options.GTAODenoisePasses % 2 != 0) ? m_GTAODenoiseImage : m_GTAOOutputImage;

			FramebufferSpecification aoFramebufferSpec;
			aoFramebufferSpec.Width = m_ViewportWidth;
			aoFramebufferSpec.Height = m_ViewportHeight;
			aoFramebufferSpec.Attachments = { ImageFormat::RGBA32F };
			aoFramebufferSpec.ExistingImages[0] = m_GeometryPass->GetOutput(0);
			aoFramebufferSpec.ClearColorOnLoad = false;
			aoFramebufferSpec.Blend = true;
			aoFramebufferSpec.BlendMode = FramebufferBlendMode::Zero_SrcColor;
			aoFramebufferSpec.DebugName = "AO-Composite";

			PipelineSpecification aoPipelineSpec;
			aoPipelineSpec.DebugName = "AO-Composite";
			aoPipelineSpec.TargetFramebuffer = Framebuffer::Create(aoFramebufferSpec);
			aoPipelineSpec.DepthTest = false;
			aoPipelineSpec.DepthWrite = false;
			aoPipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" },
			};
			aoPipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("AO-Composite");

			RenderPassSpecification aoRenderPassSpec;
			aoRenderPassSpec.DebugName = "AO-Composite";
			aoRenderPassSpec.Pipeline = Pipeline::Create(aoPipelineSpec);
			m_AOCompositePass = RenderPass::Create(aoRenderPassSpec);
			m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			m_AOCompositePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_AOCompositePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_AOCompositePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_AOCompositePass->Validate());
			m_AOCompositePass->Bake();
			m_AOCompositeMaterial = Material::Create(aoPipelineSpec.Shader, "GTAO-Composite");
		}

		// ── SSR ────────────────────────────────────────────────────────────────
		{
			ImageSpecification ssrImageSpec;
			ssrImageSpec.Format = ImageFormat::RGBA16F;
			ssrImageSpec.Usage = ImageUsage::Storage;
			ssrImageSpec.DebugName = "SSR";
			m_SSRImage = Image2D::Create(ssrImageSpec);

			ComputePassSpecification ssrComputeSpec;
			ssrComputeSpec.DebugName = "SSR-Compute";
			ssrComputeSpec.Pipeline = PipelineCompute::Create(Renderer::GetShaderLibrary()->Get("SSR"));
			m_SSRPass = ComputePass::Create(ssrComputeSpec);
			m_SSRPass->SetInput("outColor", m_SSRImage);
			m_SSRPass->SetInput("u_InputColor", m_PreConvolutedTexture.Texture);
			m_SSRPass->SetInput("u_Normal", m_GeometryPass->GetOutput(1));
			m_SSRPass->SetInput("u_HiZBuffer", m_HierarchicalDepthTexture.Texture);
			m_SSRPass->SetInput("u_MetalnessRoughness", m_GeometryPass->GetOutput(2));
			m_SSRPass->SetInput("u_VisibilityBuffer", m_PreIntegrationVisibilityTexture.Texture);
			if (m_SSRPass->IsInputValid("u_GTAOTex"))
				m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			m_SSRPass->SetInput("Camera", m_UBSCamera);
			m_SSRPass->SetInput("ScreenData", m_UBSScreenData);
			m_SSRPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_SSRPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_SSRPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_SSRPass->Validate());
			m_SSRPass->Bake();

			FramebufferSpecification ssrCompositeFBSpec;
			ssrCompositeFBSpec.Width = m_ViewportWidth;
			ssrCompositeFBSpec.Height = m_ViewportHeight;
			ssrCompositeFBSpec.Attachments = { ImageFormat::RGBA32F };
			ssrCompositeFBSpec.ExistingImages[0] = m_GeometryPass->GetOutput(0);
			ssrCompositeFBSpec.ClearColorOnLoad = false;
			ssrCompositeFBSpec.Blend = true;
			ssrCompositeFBSpec.BlendMode = FramebufferBlendMode::SrcAlphaOneMinusSrcAlpha;
			ssrCompositeFBSpec.DebugName = "SSR-Composite";

			PipelineSpecification ssrCompositePipelineSpec;
			ssrCompositePipelineSpec.DebugName = "SSR-Composite";
			ssrCompositePipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("SSR-Composite");
			ssrCompositePipelineSpec.TargetFramebuffer = Framebuffer::Create(ssrCompositeFBSpec);
			ssrCompositePipelineSpec.DepthTest = false;
			ssrCompositePipelineSpec.DepthWrite = false;
			ssrCompositePipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" },
			};

			RenderPassSpecification ssrRenderPassSpec;
			ssrRenderPassSpec.DebugName = "SSR-Composite";
			ssrRenderPassSpec.Pipeline = Pipeline::Create(ssrCompositePipelineSpec);
			m_SSRCompositePass = RenderPass::Create(ssrRenderPassSpec);
			m_SSRCompositePass->SetInput("u_SSR", m_SSRImage);
			m_SSRCompositePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_SSRCompositePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_SSRCompositePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_SSRCompositePass->Validate());
			m_SSRCompositePass->Bake();
			m_SSRCompositeMaterial = Material::Create(ssrCompositePipelineSpec.Shader, "SSR-Composite");
		}

		// ── Selected geometry (isolation for outline) ─────────────────────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { ImageFormat::RGBA32F, ImageFormat::Depth };
			fbSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			fbSpec.DepthClearValue = 0.0f;
			fbSpec.DebugName = "SelectedGeometry";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "SelectedGeometry";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("SelectedGeometry");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.DepthOperator = DepthCompareOperator::GreaterOrEqual;

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "SelectedGeometryPass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_SelectedGeometryPass = RenderPass::Create(rpSpec);
			m_SelectedGeometryPass->SetInput("Camera", m_UBSCamera);
			m_SelectedGeometryPass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_SelectedGeometryPass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_SelectedGeometryPass->Validate());
			m_SelectedGeometryPass->Bake();

			m_SelectedGeometryMaterial = Material::Create(pipelineSpec.Shader, "SelectedGeometry");
		}

		// ── Jump flood outline buffers ────────────────────────────────────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { ImageFormat::RGBA32F };
			fbSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
			fbSpec.DebugName = "JumpFlood-Init";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "JumpFlood-Init";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("JumpFlood_Init");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.DepthTest = false;
			pipelineSpec.DepthWrite = false;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" },
			};

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "JumpFlood-Init";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_JumpFloodInitPass = RenderPass::Create(rpSpec);
			m_JumpFloodInitPass->SetInput("u_Texture", m_SelectedGeometryPass->GetOutput(0));
			m_JumpFloodInitPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_JumpFloodInitPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_JumpFloodInitPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_JumpFloodInitPass->Validate());
			m_JumpFloodInitPass->Bake();
			m_JumpFloodInitMaterial = Material::Create(pipelineSpec.Shader, "JumpFlood-Init");

			for (uint32_t i = 0; i < 2; i++)
			{
				fbSpec.DebugName = "JumpFlood-Pass" + std::to_string(i);
				pipelineSpec.DebugName = "JumpFlood-Pass" + std::to_string(i);
				pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("JumpFlood_Pass");
				pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);

				rpSpec.DebugName = pipelineSpec.DebugName;
				rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
				m_JumpFloodPasses[i] = RenderPass::Create(rpSpec);
				m_JumpFloodPasses[i]->SetInput("u_Texture", i == 0 ? m_JumpFloodInitPass->GetOutput(0) : m_JumpFloodPasses[0]->GetOutput(0));
				m_JumpFloodPasses[i]->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
				m_JumpFloodPasses[i]->SetInput("r_PointSampler", Renderer::GetPointSampler());
				m_JumpFloodPasses[i]->SetInput("r_LinearSampler", Renderer::GetClampSampler());
				LUX_CORE_VERIFY(m_JumpFloodPasses[i]->Validate());
				m_JumpFloodPasses[i]->Bake();
				m_JumpFloodPassMaterials[i] = Material::Create(pipelineSpec.Shader, pipelineSpec.DebugName);
			}
		}

		// ── Wireframe pass (on top of geometry, for selected meshes) ──────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.ExistingImages[0] = m_GeometryPassFramebuffer->GetImage(0);
			fbSpec.Attachments = { ImageFormat::RGBA32F };
			fbSpec.ClearColorOnLoad = false;
			fbSpec.DebugName = "GeometryWireframe";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "GeometryWireframe";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("Wireframe");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.Layout = vertexLayout;
			pipelineSpec.Wireframe = true;
			pipelineSpec.DepthTest = false;

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "GeometryWireframePass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_GeometryWireframePass = RenderPass::Create(rpSpec);
			m_GeometryWireframePass->SetInput("Camera", m_UBSCamera);
			m_GeometryWireframePass->SetInput("InstanceTransforms", m_SBSInstanceTransforms);
			m_GeometryWireframePass->SetInput("ObjectIndexes", m_SBSObjectIndexes);
			LUX_CORE_VERIFY(m_GeometryWireframePass->Validate());
			m_GeometryWireframePass->Bake();

			m_WireframeMaterial = Material::Create(pipelineSpec.Shader, "Wireframe");
			m_WireframeMaterial->Set("u_MaterialUniforms.Color", glm::vec4{ 1.0f, 0.5f, 0.0f, 1.0f });
		}

		// ── Skybox (renders into geometry color attachment before geometry) ────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.ExistingImages[0] = m_GeometryPassFramebuffer->GetImage(0);
			fbSpec.Attachments = { ImageFormat::RGBA32F };
			fbSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			fbSpec.DebugName = "Skybox";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "Skybox";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("Skybox");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.DepthWrite = false;
			pipelineSpec.DepthTest = false;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};
			m_SkyboxPipeline = Pipeline::Create(pipelineSpec);
			m_SkyboxMaterial = Material::Create(pipelineSpec.Shader, "Skybox");
			m_SkyboxMaterial->SetFlag(MaterialFlag::DepthTest, false);

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "SkyboxPass";
			rpSpec.Pipeline = m_SkyboxPipeline;
			m_SkyboxPass = RenderPass::Create(rpSpec);
			m_SkyboxPass->SetInput("Camera", m_UBSCamera);
			LUX_CORE_VERIFY(m_SkyboxPass->Validate());
			m_SkyboxPass->Bake();
		}

		// ── Bloom compute (feeds the scene composite) ─────────────────────────
		{
			Ref<Shader> shader = Renderer::GetShaderLibrary()->Get("Bloom");
			m_BloomComputePipeline = PipelineCompute::Create(shader);

			TextureSpecification spec;
			spec.Format = ImageFormat::RGBA32F;
			spec.Width = 1;
			spec.Height = 1;
			spec.SamplerWrap = TextureWrap::Clamp;
			spec.Storage = true;
			spec.GenerateMips = true;

			for (uint32_t i = 0; i < (uint32_t)m_BloomComputeTextures.size(); i++)
			{
				spec.DebugName = "BloomCompute-" + std::to_string(i);
				m_BloomComputeTextures[i].Texture = Texture2D::Create(spec);
			}

			ComputePassSpecification computeSpec;
			computeSpec.DebugName = "Bloom-Compute";
			computeSpec.Pipeline = m_BloomComputePipeline;
			m_BloomComputePass = ComputePass::Create(computeSpec);
			m_BloomComputePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_BloomComputePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_BloomComputePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_BloomComputePass->Validate());
			m_BloomComputePass->Bake();

			m_BloomDirtTexture = Renderer::GetBlackTexture();
			ResizeBloomResources();
			CreateBloomPassMaterials();
		}

		// ── Scene composite (tone-map + exposure + opacity) ───────────────────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.Attachments = { ImageFormat::RGBA, ImageFormat::DEPTH32FSTENCIL8UINT };
			fbSpec.ExistingImages[1] = m_PreDepthPass->GetDepthOutput();
			fbSpec.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			fbSpec.ClearColorOnLoad = false;
			fbSpec.ClearDepthOnLoad = false;
			fbSpec.DebugName = "SceneComposite";
			m_CompositingFramebuffer = Framebuffer::Create(fbSpec);

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "SceneComposite";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("SceneComposite");
			pipelineSpec.TargetFramebuffer = m_CompositingFramebuffer;
			pipelineSpec.DepthWrite = false;
			pipelineSpec.DepthTest = false;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};

			m_CompositeMaterial = Material::Create(pipelineSpec.Shader, "SceneComposite");
			m_CompositeMaterial->Set("u_Uniforms.Exposure", 1.0f);
			m_CompositeMaterial->Set("u_Uniforms.BloomIntensity", 0.0f);
			m_CompositeMaterial->Set("u_Uniforms.BloomDirtIntensity", 0.0f);
			m_CompositeMaterial->Set("u_Uniforms.Opacity", m_Opacity);
			m_CompositeMaterial->Set("u_Uniforms.Time", 0.0f);

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "CompositePass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_CompositePass = RenderPass::Create(rpSpec);
			// The geometry color output feeds the composite shader
			m_CompositePass->SetInput("u_Texture", m_GeometryPass->GetOutput(0));
			m_CompositePass->SetInput("u_BloomTexture", m_BloomComputeTextures[2].Texture);
			m_CompositePass->SetInput("u_BloomDirtTexture", m_BloomDirtTexture);
			m_CompositePass->SetInput("u_DepthTexture", m_PreDepthPass->GetDepthOutput());
			m_CompositePass->SetInput("u_TransparentDepthTexture", m_GeometryPassTransparent->GetDepthOutput());
			LUX_CORE_VERIFY(m_CompositePass->Validate());
			m_CompositePass->Bake();
		}

		// ── Depth of field and jump-flood composite overlays ─────────────────
		{
			FramebufferSpecification dofFBSpec;
			dofFBSpec.Width = m_ViewportWidth;
			dofFBSpec.Height = m_ViewportHeight;
			dofFBSpec.Attachments = { ImageFormat::RGBA, ImageFormat::DEPTH32FSTENCIL8UINT };
			dofFBSpec.ExistingImages[1] = m_PreDepthPass->GetDepthOutput();
			dofFBSpec.ClearColorOnLoad = false;
			dofFBSpec.ClearDepthOnLoad = false;
			dofFBSpec.DebugName = "POST-DepthOfField";

			PipelineSpecification dofPipelineSpec;
			dofPipelineSpec.DebugName = "POST-DepthOfField";
			dofPipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("DOF");
			dofPipelineSpec.TargetFramebuffer = Framebuffer::Create(dofFBSpec);
			dofPipelineSpec.DepthTest = false;
			dofPipelineSpec.DepthWrite = false;
			dofPipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};

			RenderPassSpecification dofPassSpec;
			dofPassSpec.DebugName = "POST-DepthOfField";
			dofPassSpec.Pipeline = Pipeline::Create(dofPipelineSpec);
			m_DOFPass = RenderPass::Create(dofPassSpec);
			m_DOFPass->SetInput("u_Texture", m_CompositePass->GetOutput(0));
			m_DOFPass->SetInput("u_DepthTexture", m_PreDepthPass->GetDepthOutput());
			m_DOFPass->SetInput("Camera", m_UBSCamera);
			m_DOFPass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_DOFPass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_DOFPass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_DOFPass->Validate());
			m_DOFPass->Bake();
			m_DOFMaterial = Material::Create(dofPipelineSpec.Shader, "DepthOfField");

			FramebufferSpecification jfCompositeFBSpec;
			jfCompositeFBSpec.Width = m_ViewportWidth;
			jfCompositeFBSpec.Height = m_ViewportHeight;
			jfCompositeFBSpec.Attachments = { ImageFormat::RGBA, ImageFormat::DEPTH32FSTENCIL8UINT };
			jfCompositeFBSpec.ExistingImages[0] = m_CompositingFramebuffer->GetImage(0);
			jfCompositeFBSpec.ExistingImages[1] = m_CompositingFramebuffer->GetDepthImage();
			jfCompositeFBSpec.ClearColorOnLoad = false;
			jfCompositeFBSpec.ClearDepthOnLoad = false;
			jfCompositeFBSpec.Blend = true;
			jfCompositeFBSpec.BlendMode = FramebufferBlendMode::SrcAlphaOneMinusSrcAlpha;
			jfCompositeFBSpec.DebugName = "JumpFlood-Composite";

			PipelineSpecification jfCompositePipelineSpec;
			jfCompositePipelineSpec.DebugName = "JumpFlood-Composite";
			jfCompositePipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("JumpFlood_Composite");
			jfCompositePipelineSpec.TargetFramebuffer = Framebuffer::Create(jfCompositeFBSpec);
			jfCompositePipelineSpec.DepthTest = false;
			jfCompositePipelineSpec.DepthWrite = false;
			jfCompositePipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};

			RenderPassSpecification jfCompositePassSpec;
			jfCompositePassSpec.DebugName = "JumpFlood-Composite";
			jfCompositePassSpec.Pipeline = Pipeline::Create(jfCompositePipelineSpec);
			m_JumpFloodCompositePass = RenderPass::Create(jfCompositePassSpec);
			m_JumpFloodCompositePass->SetInput("u_Texture", m_JumpFloodPasses[0]->GetOutput(0));
			m_JumpFloodCompositePass->SetInput("r_DefaultSampler", Renderer::GetDefaultSampler());
			m_JumpFloodCompositePass->SetInput("r_PointSampler", Renderer::GetPointSampler());
			m_JumpFloodCompositePass->SetInput("r_LinearSampler", Renderer::GetClampSampler());
			LUX_CORE_VERIFY(m_JumpFloodCompositePass->Validate());
			m_JumpFloodCompositePass->Bake();
			m_JumpFloodCompositeMaterial = Material::Create(jfCompositePipelineSpec.Shader, "JumpFlood-Composite");
		}

		// ── Editor grid (renders into composite output, preserves depth) ──────
		{
			FramebufferSpecification fbSpec;
			fbSpec.Width = m_ViewportWidth;
			fbSpec.Height = m_ViewportHeight;
			fbSpec.ExistingImages[0] = m_CompositingFramebuffer->GetImage(0);
			fbSpec.ExistingImages[1] = m_CompositingFramebuffer->GetDepthImage();
			fbSpec.Attachments = { ImageFormat::RGBA, ImageFormat::DEPTH32FSTENCIL8UINT };
			fbSpec.ClearColorOnLoad = false;
			fbSpec.ClearDepthOnLoad = false;
			fbSpec.DebugName = "Grid";

			PipelineSpecification pipelineSpec;
			pipelineSpec.DebugName = "Grid";
			pipelineSpec.Shader = Renderer::GetShaderLibrary()->Get("Grid");
			pipelineSpec.TargetFramebuffer = Framebuffer::Create(fbSpec);
			pipelineSpec.DepthTest = true;
			pipelineSpec.DepthWrite = false;
			pipelineSpec.Layout = {
				{ ShaderDataType::Float3, "a_Position" },
				{ ShaderDataType::Float2, "a_TexCoord" }
			};

			RenderPassSpecification rpSpec;
			rpSpec.DebugName = "GridPass";
			rpSpec.Pipeline = Pipeline::Create(pipelineSpec);
			m_GridRenderPass = RenderPass::Create(rpSpec);
			m_GridRenderPass->SetInput("Camera", m_UBSCamera);
			LUX_CORE_VERIFY(m_GridRenderPass->Validate());
			m_GridRenderPass->Bake();

			constexpr float gridScale = 16.025f;
			constexpr float gridSize = 0.025f;
			m_GridMaterial = Material::Create(pipelineSpec.Shader, "Grid");
			const static glm::mat4 transform =
				glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f))
				* glm::scale(glm::mat4(1.0f), glm::vec3(8.0f));
			m_GridMaterial->Set("u_Settings.Transform", transform);
			m_GridMaterial->Set("u_Settings.Scale", gridScale);
			m_GridMaterial->Set("u_Settings.Size", gridSize);
		}

		// ── Physics collider debug materials ─────────────────────────────────
		{
			Ref<Shader> wireframeShader = Renderer::GetShaderLibrary()->Get("Wireframe");
			m_SimpleColliderMaterial = Material::Create(wireframeShader, "SimpleCollider");
			m_SimpleColliderMaterial->Set("u_MaterialUniforms.Color", glm::vec4{ 0.2f, 1.0f, 0.2f, 1.0f });
			m_ComplexColliderMaterial = Material::Create(wireframeShader, "ComplexCollider");
			m_ComplexColliderMaterial->Set("u_MaterialUniforms.Color", glm::vec4{ 0.5f, 0.5f, 1.0f, 1.0f });
		}

		ResizeScreenSpaceEffectResources();

		// Signal render thread that GPU resources are ready
		Renderer::Submit([instance = Ref<SceneRenderer>(this)]() mutable {
			instance->m_ResourcesCreatedGPU = true;
			});
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Viewport resize
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		width = (uint32_t)(width * m_Specification.Tiering.RendererScale);
		height = (uint32_t)(height * m_Specification.Tiering.RendererScale);

		if (m_ViewportWidth != width || m_ViewportHeight != height)
		{
			m_ViewportWidth = width;
			m_ViewportHeight = height;
			m_InvViewportWidth = width > 0 ? 1.0f / (float)width : 0.0f;
			m_InvViewportHeight = height > 0 ? 1.0f / (float)height : 0.0f;
			m_NeedsResize = true;
		}
	}

	void SceneRenderer::ResizeLightCullingResources()
	{
		m_LightTilesCountX = glm::max(1u, (m_ViewportWidth + LightCullingTileSize - 1u) / LightCullingTileSize);
		m_LightTilesCountY = glm::max(1u, (m_ViewportHeight + LightCullingTileSize - 1u) / LightCullingTileSize);

		const uint64_t tileCount = static_cast<uint64_t>(m_LightTilesCountX) * static_cast<uint64_t>(m_LightTilesCountY);
		const uint64_t bufferSize = tileCount * MaxVisibleLightsPerTile * sizeof(int32_t);
		LUX_CORE_ASSERT(bufferSize <= std::numeric_limits<uint32_t>::max(), "Light culling index buffer is too large");

		const uint32_t newSize = static_cast<uint32_t>(bufferSize);
		if (newSize == m_VisibleLightIndexBufferSize)
			return;

		m_VisibleLightIndexBufferSize = newSize;
		m_SBSVisiblePointLightIndices->Resize(newSize);
		m_SBSVisibleSpotLightIndices->Resize(newSize);
	}

	void SceneRenderer::ResizeBloomResources()
	{
		if (!m_BloomComputePass || m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;

		glm::uvec2 bloomSize = { glm::max(1u, (m_ViewportWidth + 1u) / 2u), glm::max(1u, (m_ViewportHeight + 1u) / 2u) };
		bloomSize.x = glm::max(m_BloomComputeWorkgroupSize, AlignUp(bloomSize.x, m_BloomComputeWorkgroupSize));
		bloomSize.y = glm::max(m_BloomComputeWorkgroupSize, AlignUp(bloomSize.y, m_BloomComputeWorkgroupSize));

		ImageViewSpecification imageViewSpec;
		imageViewSpec.MipCount = 1;

		for (uint32_t i = 0; i < (uint32_t)m_BloomComputeTextures.size(); i++)
		{
			auto& bloomTexture = m_BloomComputeTextures[i];
			bloomTexture.Texture->Resize(bloomSize);

			const uint32_t mipCount = bloomTexture.Texture->GetMipLevelCount();
			bloomTexture.ImageViews.resize(mipCount);

			imageViewSpec.Image = bloomTexture.Texture->GetImage();
			imageViewSpec.DebugName = "BloomCompute-" + std::to_string(i);

			for (uint32_t mip = 0; mip < mipCount; mip++)
			{
				imageViewSpec.Mip = mip;
				bloomTexture.ImageViews[mip] = ImageView::Create(imageViewSpec);
			}
		}
	}

	void SceneRenderer::CreateBloomPassMaterials()
	{
		if (!m_BloomComputePass || !m_SkyboxPass || !m_BloomComputeTextures[0].Texture)
			return;

		Ref<Image2D> inputImage = m_SkyboxPass->GetOutput(0);
		const uint32_t mipCount = m_BloomComputeTextures[0].Texture->GetMipLevelCount();
		if (mipCount < 4)
			return;

		const uint32_t mips = mipCount - 2;

		m_BloomComputeMaterials.PrefilterMaterial = Material::Create(m_BloomComputePass->GetShader(), "Bloom-Prefilter");
		m_BloomComputeMaterials.PrefilterMaterial->Set("o_Image", m_BloomComputeTextures[0].ImageViews[0]);
		m_BloomComputeMaterials.PrefilterMaterial->Set("u_Texture", inputImage);
		m_BloomComputeMaterials.PrefilterMaterial->Set("u_BloomTexture", inputImage);

		m_BloomComputeMaterials.DownsampleAMaterials.clear();
		m_BloomComputeMaterials.DownsampleBMaterials.clear();
		m_BloomComputeMaterials.DownsampleAMaterials.resize(mips);
		m_BloomComputeMaterials.DownsampleBMaterials.resize(mips);

		for (uint32_t i = 1; i < mips; i++)
		{
			m_BloomComputeMaterials.DownsampleAMaterials[i] = Material::Create(m_BloomComputePass->GetShader(), "Bloom-DownsampleA");
			m_BloomComputeMaterials.DownsampleAMaterials[i]->Set("o_Image", m_BloomComputeTextures[1].ImageViews[i]);
			m_BloomComputeMaterials.DownsampleAMaterials[i]->Set("u_Texture", m_BloomComputeTextures[0].Texture);
			m_BloomComputeMaterials.DownsampleAMaterials[i]->Set("u_BloomTexture", inputImage);

			m_BloomComputeMaterials.DownsampleBMaterials[i] = Material::Create(m_BloomComputePass->GetShader(), "Bloom-DownsampleB");
			m_BloomComputeMaterials.DownsampleBMaterials[i]->Set("o_Image", m_BloomComputeTextures[0].ImageViews[i]);
			m_BloomComputeMaterials.DownsampleBMaterials[i]->Set("u_Texture", m_BloomComputeTextures[1].Texture);
			m_BloomComputeMaterials.DownsampleBMaterials[i]->Set("u_BloomTexture", inputImage);
		}

		m_BloomComputeMaterials.FirstUpsampleMaterial = Material::Create(m_BloomComputePass->GetShader(), "Bloom-FirstUpsample");
		m_BloomComputeMaterials.FirstUpsampleMaterial->Set("o_Image", m_BloomComputeTextures[2].ImageViews[mips - 2]);
		m_BloomComputeMaterials.FirstUpsampleMaterial->Set("u_Texture", m_BloomComputeTextures[0].Texture);
		m_BloomComputeMaterials.FirstUpsampleMaterial->Set("u_BloomTexture", inputImage);

		m_BloomComputeMaterials.UpsampleMaterials.clear();
		m_BloomComputeMaterials.UpsampleMaterials.resize(mips - 2);

		for (int32_t mip = (int32_t)mips - 3; mip >= 0; mip--)
		{
			m_BloomComputeMaterials.UpsampleMaterials[mip] = Material::Create(m_BloomComputePass->GetShader(), "Bloom-Upsample");
			m_BloomComputeMaterials.UpsampleMaterials[mip]->Set("o_Image", m_BloomComputeTextures[2].ImageViews[mip]);
			m_BloomComputeMaterials.UpsampleMaterials[mip]->Set("u_Texture", m_BloomComputeTextures[0].Texture);
			m_BloomComputeMaterials.UpsampleMaterials[mip]->Set("u_BloomTexture", m_BloomComputeTextures[2].Texture);
		}
	}

	void SceneRenderer::ResizeScreenSpaceEffectResources()
	{
		if (m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;

		const glm::uvec2 viewportSize{ glm::max(1u, m_ViewportWidth), glm::max(1u, m_ViewportHeight) };

		auto resizePass = [&](Ref<RenderPass> pass)
		{
			if (pass && pass->GetTargetFramebuffer())
				pass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
		};

		resizePass(m_AOCompositePass);
		resizePass(m_SSRCompositePass);
		resizePass(m_JumpFloodInitPass);
		resizePass(m_JumpFloodPasses[0]);
		resizePass(m_JumpFloodPasses[1]);
		resizePass(m_JumpFloodCompositePass);
		resizePass(m_DOFPass);

		// HZB uses a power-of-two texture with UV factor back to the real viewport.
		if (m_HierarchicalDepthTexture.Texture)
		{
			const uint32_t hzbWidth = NextPowerOfTwo(viewportSize.x);
			const uint32_t hzbHeight = NextPowerOfTwo(viewportSize.y);
			const uint32_t maxDimension = glm::max(hzbWidth, hzbHeight);
			m_SSROptions.NumDepthMips = glm::max(1u, (uint32_t)glm::floor(glm::log2((float)maxDimension)) + 1u);
			m_SSROptions.HZBUvFactor = glm::vec2(viewportSize) / glm::vec2(hzbWidth, hzbHeight);

			m_HierarchicalDepthTexture.Texture->Resize(hzbWidth, hzbHeight);
			const uint32_t mipCount = m_HierarchicalDepthTexture.Texture->GetMipLevelCount();
			m_HierarchicalDepthTexture.ImageViews.resize(mipCount);

			ImageViewSpecification viewSpec;
			viewSpec.Image = m_HierarchicalDepthTexture.Texture->GetImage();
			viewSpec.MipCount = 1;
			for (uint32_t mip = 0; mip < mipCount; mip++)
			{
				viewSpec.Mip = mip;
				viewSpec.DebugName = "HierarchicalDepthTexture-" + std::to_string(mip);
				m_HierarchicalDepthTexture.ImageViews[mip] = ImageView::Create(viewSpec);
			}

			CreateHZBPassMaterials();
		}

		if (m_PreIntegrationVisibilityTexture.Texture)
		{
			m_PreIntegrationVisibilityTexture.Texture->Resize(viewportSize);
			const uint32_t mipCount = m_PreIntegrationVisibilityTexture.Texture->GetMipLevelCount();
			m_PreIntegrationVisibilityTexture.ImageViews.resize(mipCount > 0 ? mipCount - 1 : 0);

			ImageViewSpecification viewSpec;
			viewSpec.Image = m_PreIntegrationVisibilityTexture.Texture->GetImage();
			viewSpec.MipCount = 1;
			for (uint32_t mip = 1; mip < mipCount; mip++)
			{
				viewSpec.Mip = mip;
				viewSpec.DebugName = "PreIntegrationVisibilityTexture-" + std::to_string(mip);
				m_PreIntegrationVisibilityTexture.ImageViews[mip - 1] = ImageView::Create(viewSpec);
			}

			CreatePreIntegrationPassMaterials();
		}

		if (m_Options.EnableGTAO && m_GTAOOutputImage && m_GTAODenoiseImage && m_GTAOEdgesOutputImage)
		{
			glm::uvec2 gtaoSize = m_GTAODataCB.HalfRes ? (viewportSize + 1u) / 2u : viewportSize;
			glm::uvec2 denoiseSize = gtaoSize;
			const ImageFormat gtaoImageFormat = m_Options.GTAOBentNormals ? ImageFormat::RED32UI : ImageFormat::RED8UI;
			m_GTAOOutputImage->GetSpecification().Format = gtaoImageFormat;
			m_GTAODenoiseImage->GetSpecification().Format = gtaoImageFormat;

			constexpr uint32_t GTAO_WORKGROUP_SIZE = 16u;
			gtaoSize = { AlignUp(gtaoSize.x, GTAO_WORKGROUP_SIZE), AlignUp(gtaoSize.y, GTAO_WORKGROUP_SIZE) };
			m_GTAOOutputImage->Resize(gtaoSize.x, gtaoSize.y);
			m_GTAOEdgesOutputImage->Resize(gtaoSize.x, gtaoSize.y);

			m_GTAOWorkGroups = { gtaoSize.x / GTAO_WORKGROUP_SIZE, gtaoSize.y / GTAO_WORKGROUP_SIZE, 1 };

			constexpr uint32_t DENOISE_WORKGROUP_SIZE = 8u;
			denoiseSize = { AlignUp(denoiseSize.x, DENOISE_WORKGROUP_SIZE), AlignUp(denoiseSize.y, DENOISE_WORKGROUP_SIZE) };
			m_GTAODenoiseImage->Resize(denoiseSize.x, denoiseSize.y);
			m_GTAODenoiseWorkGroups = {
				(denoiseSize.x + 2u * DENOISE_WORKGROUP_SIZE - 1u) / (DENOISE_WORKGROUP_SIZE * 2u),
				denoiseSize.y / DENOISE_WORKGROUP_SIZE,
				1
			};

			m_GTAOFinalImage = (m_Options.GTAODenoisePasses % 2 != 0) ? m_GTAODenoiseImage : m_GTAOOutputImage;
			if (m_AOCompositePass)
				m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
				m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
		}

		if (m_SSRImage && m_PreConvolutedTexture.Texture)
		{
			constexpr uint32_t SSR_WORKGROUP_SIZE = 8u;
			glm::uvec2 ssrSize = m_SSROptions.HalfRes ? (viewportSize + 1u) / 2u : viewportSize;
			ssrSize = { AlignUp(ssrSize.x, SSR_WORKGROUP_SIZE), AlignUp(ssrSize.y, SSR_WORKGROUP_SIZE) };

			m_SSRImage->Resize(ssrSize.x, ssrSize.y);
			m_SSRWorkGroups = { ssrSize.x / SSR_WORKGROUP_SIZE, ssrSize.y / SSR_WORKGROUP_SIZE, 1 };

			m_PreConvolutedTexture.Texture->Resize(ssrSize.x, ssrSize.y);
			const uint32_t mipCount = m_PreConvolutedTexture.Texture->GetMipLevelCount();
			m_PreConvolutedTexture.ImageViews.resize(mipCount);

			ImageViewSpecification viewSpec;
			viewSpec.Image = m_PreConvolutedTexture.Texture->GetImage();
			viewSpec.MipCount = 1;
			for (uint32_t mip = 0; mip < mipCount; mip++)
			{
				viewSpec.Mip = mip;
				viewSpec.DebugName = "PreConvolutionCompute-" + std::to_string(mip);
				m_PreConvolutedTexture.ImageViews[mip] = ImageView::Create(viewSpec);
			}

			CreatePreConvolutionPassMaterials();
		}

		ResizeBloomResources();
		CreateBloomPassMaterials();
	}

	void SceneRenderer::CreateHZBPassMaterials()
	{
		if (!m_HierarchicalDepthPass || !m_PreDepthPass || !m_HierarchicalDepthTexture.Texture)
			return;

		constexpr uint32_t maxMipBatchSize = 4;
		const uint32_t hzbMipCount = m_HierarchicalDepthTexture.Texture->GetMipLevelCount();
		m_HZBMaterials.clear();
		m_HZBMaterials.resize(DivideRoundUp(hzbMipCount, maxMipBatchSize));

		for (uint32_t startDestMip = 0; startDestMip < hzbMipCount; startDestMip += maxMipBatchSize)
		{
			Ref<Material> material = Material::Create(m_HierarchicalDepthPass->GetShader(), "HZB");
			material->Set("u_InputDepth", startDestMip == 0 ? m_PreDepthPass->GetDepthOutput() : m_HierarchicalDepthTexture.Texture->GetImage());

			for (uint32_t outputIndex = 0; outputIndex < maxMipBatchSize; outputIndex++)
			{
				const uint32_t destMip = glm::min(startDestMip + outputIndex, hzbMipCount - 1u);
				material->Set("o_HZB", m_HierarchicalDepthTexture.ImageViews[destMip], outputIndex);
			}

			m_HZBMaterials[startDestMip / maxMipBatchSize] = material;
		}
	}

	void SceneRenderer::CreatePreIntegrationPassMaterials()
	{
		if (!m_PreIntegrationPass || !m_PreIntegrationVisibilityTexture.Texture || !m_HierarchicalDepthTexture.Texture)
			return;

		const uint32_t mipCount = m_PreIntegrationVisibilityTexture.Texture->GetMipLevelCount();
		if (mipCount < 2)
			return;

		m_PreIntegrationMaterials.clear();
		m_PreIntegrationMaterials.resize(mipCount - 1);

		for (uint32_t mip = 1; mip < mipCount; mip++)
		{
			Ref<Material> material = Material::Create(m_PreIntegrationPass->GetShader(), "Pre-Integration");
			material->Set("o_VisibilityImage", m_PreIntegrationVisibilityTexture.ImageViews[mip - 1]);
			material->Set("u_VisibilityTex", m_PreIntegrationVisibilityTexture.Texture);
			material->Set("u_HZB", m_HierarchicalDepthTexture.Texture);
			m_PreIntegrationMaterials[mip - 1] = material;
		}
	}

	void SceneRenderer::CreatePreConvolutionPassMaterials()
	{
		if (!m_PreConvolutionComputePass || !m_SkyboxPass || !m_PreConvolutedTexture.Texture)
			return;

		const uint32_t mipCount = m_PreConvolutedTexture.Texture->GetMipLevelCount();
		m_PreConvolutionMaterials.clear();
		m_PreConvolutionMaterials.resize(mipCount);

		for (uint32_t mip = 0; mip < mipCount; mip++)
		{
			Ref<Material> material = Material::Create(m_PreConvolutionComputePass->GetShader(), "Pre-Convolution");
			material->Set("o_Image", m_PreConvolutedTexture.ImageViews[mip]);
			material->Set("u_Input", mip == 0 ? m_SkyboxPass->GetOutput(0) : m_PreConvolutedTexture.Texture->GetImage());
			m_PreConvolutionMaterials[mip] = material;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Per-frame scene data setters (call between BeginScene and EndScene)
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::SetLightEnvironment(const LightEnvironment& lightEnvironment)
	{
		m_SceneData.SceneLightEnvironment = lightEnvironment;
	}

	void SceneRenderer::SetEnvironment(Ref<Environment> environment, float intensity, float skyboxLod)
	{
		m_SceneData.SceneEnvironment = environment;
		m_SceneData.SceneEnvironmentIntensity = intensity;
		m_SceneData.SkyboxLod = skyboxLod;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// BeginScene
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::BeginScene(const SceneRendererCamera& camera)
	{
		LUX_CORE_ASSERT(m_Scene, "No scene attached to SceneRenderer");
		LUX_CORE_ASSERT(!m_Active, "BeginScene called twice without EndScene");
		m_Active = true;

		if (m_ResourcesCreatedGPU)
			m_ResourcesCreated = true;

		if (!m_ResourcesCreated)
			return; // GPU resources not yet available

		// Open the upload command buffer for uniform/storage buffer writes
		m_UploadCommandBuffer->Begin();

		m_SceneData.SceneCamera = camera;

		// ── Handle viewport resize ────────────────────────────────────────────
		if (m_NeedsResize)
		{
			m_NeedsResize = false;
			m_ScreenSpaceProjectionMatrix = glm::ortho(0.0f, (float)m_ViewportWidth, 0.0f, (float)m_ViewportHeight);

			m_PreDepthPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPassFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryPassTransparent->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SkyboxPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_SelectedGeometryPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GeometryWireframePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_CompositingFramebuffer->Resize(m_ViewportWidth, m_ViewportHeight);
			m_CompositePass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			m_GridRenderPass->GetTargetFramebuffer()->Resize(m_ViewportWidth, m_ViewportHeight);
			ResizeLightCullingResources();
			ResizeScreenSpaceEffectResources();
		}

		// ── Camera uniform buffer ─────────────────────────────────────────────
		{
			const glm::mat4 viewProj = camera.Camera.GetProjectionMatrix() * camera.ViewMatrix;
			const glm::mat4 viewInverse = glm::inverse(camera.ViewMatrix);
			const glm::mat4 projInverse = glm::inverse(camera.Camera.GetProjectionMatrix());

			m_CameraUB.ViewProjection = viewProj;
			m_CameraUB.InverseViewProjection = viewInverse * projInverse;
			m_CameraUB.Projection = camera.Camera.GetProjectionMatrix();
			m_CameraUB.InverseProjection = projInverse;
			m_CameraUB.View = camera.ViewMatrix;
			m_CameraUB.InverseView = viewInverse;

			// Depth linearization for GTAO-style effects (kept for forward compat)
			float depthLinearizeMul = -m_CameraUB.Projection[3][2];
			float depthLinearizeAdd = m_CameraUB.Projection[2][2];
			if (depthLinearizeMul * depthLinearizeAdd < 0) depthLinearizeAdd = -depthLinearizeAdd;
			m_CameraUB.DepthUnpackConsts = { depthLinearizeMul, depthLinearizeAdd };

			const float* P = glm::value_ptr(camera.Camera.GetProjectionMatrix());
			m_CameraUB.NDCToViewMul = { 2.0f / P[0],  2.0f / P[5] };
			m_CameraUB.NDCToViewAdd = { -1.0f,        -1.0f };
			m_CameraUB.CameraTanHalfFOV = { 1.0f / P[0],  1.0f / P[5] };

			auto cameraData = m_CameraUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, cameraData]() mutable {
				instance->m_UBSCamera->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &cameraData, sizeof(UBCamera));
				});
		}

		// ── Screen uniform buffer ─────────────────────────────────────────────
		{
			const glm::vec2 fullResolution = {
				glm::max(1.0f, static_cast<float>(m_ViewportWidth)),
				glm::max(1.0f, static_cast<float>(m_ViewportHeight))
			};
			const glm::vec2 halfResolution = glm::max(fullResolution * 0.5f, glm::vec2(1.0f));

			m_ScreenDataUB.FullResolution = fullResolution;
			m_ScreenDataUB.InvFullResolution = 1.0f / fullResolution;
			m_ScreenDataUB.HalfResolution = halfResolution;
			m_ScreenDataUB.InvHalfResolution = 1.0f / halfResolution;

			auto screenData = m_ScreenDataUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, screenData]() mutable {
				instance->m_UBSScreenData->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &screenData, sizeof(UBScreenData));
				});
		}

		// ── Screen-space effect constants ────────────────────────────────────
		{
			const glm::vec2 gtaoPixelSize = m_GTAODataCB.HalfRes
				? m_ScreenDataUB.InvHalfResolution
				: m_ScreenDataUB.InvFullResolution;
			m_GTAODataCB.NDCToViewMul_x_PixelSize = m_CameraUB.NDCToViewMul * gtaoPixelSize;
			m_GTAODataCB.HZBUVFactor = m_SSROptions.HZBUvFactor;
			m_GTAODataCB.NoiseIndex = (int)(Renderer::GetCurrentFrameIndex() % 64);
			m_GTAODataCB.ShadowTolerance = 0.0f;
		}

		// ── Scene (light) uniform buffer ──────────────────────────────────────
		{
			const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
			m_SceneUB.Lights.Direction = dirLight.Direction;
			m_SceneUB.Lights.Radiance = dirLight.Radiance;
			m_SceneUB.Lights.Intensity = dirLight.Intensity;
			m_SceneUB.Lights.ShadowAmount = dirLight.ShadowAmount;
			m_SceneUB.CameraPosition = glm::vec3(glm::inverse(camera.ViewMatrix)[3]);
			m_SceneUB.EnvironmentMapIntensity = m_SceneData.SceneEnvironmentIntensity;

			auto sceneData = m_SceneUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, sceneData]() mutable {
				instance->m_UBSScene->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &sceneData, sizeof(UBScene));
				});
		}

		// ── Point lights uniform buffer ───────────────────────────────────────
		{
			const auto& pointLights = m_SceneData.SceneLightEnvironment.PointLights;
			m_PointLightsUB.Count = (uint32_t)glm::min((size_t)256, pointLights.size());
			if (m_PointLightsUB.Count > 0)
				std::memcpy(m_PointLightsUB.PointLights, pointLights.data(),
					sizeof(PointLight) * m_PointLightsUB.Count);

			auto plData = m_PointLightsUB;
			uint32_t plSize = (uint32_t)(16ull + sizeof(PointLight) * plData.Count);
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, plData, plSize]() mutable {
				instance->m_UBSPointLights->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &plData, plSize);
				});
		}

		// ── Spot lights uniform buffer + shadow data ─────────────────────────-
		{
			const auto& spotLights = m_SceneData.SceneLightEnvironment.SpotLights;
			m_SpotLightsUB.Count = (uint32_t)glm::min((size_t)256, spotLights.size());
			m_SpotShadowCount = 0;

			uint32_t castCount = 0;
			for (size_t i = 0; i < spotLights.size(); i++)
				if (spotLights[i].CastsShadows)
					castCount++;

			m_SpotShadowAtlasGridSize = (uint32_t)glm::ceil(glm::sqrt((float)glm::max(1u, glm::min((uint32_t)MaxSpotShadows, castCount))));
			m_SpotShadowAtlasGridSize = glm::max(1u, glm::min(m_SpotShadowAtlasGridSize, 4u));
			m_SpotShadowTileSize = m_SpotShadowMapSize / m_SpotShadowAtlasGridSize;

			if (m_SpotShadowMapPass && m_SpotShadowMapPass->GetTargetFramebuffer()
				&& (m_SpotShadowMapPass->GetTargetFramebuffer()->GetWidth() != m_SpotShadowMapSize
					|| m_SpotShadowMapPass->GetTargetFramebuffer()->GetHeight() != m_SpotShadowMapSize))
			{
				m_SpotShadowMapPass->GetTargetFramebuffer()->Resize(m_SpotShadowMapSize, m_SpotShadowMapSize);
			}

			if (m_SpotLightsUB.Count > 0)
				std::memcpy(m_SpotLightsUB.SpotLights, spotLights.data(),
					sizeof(SpotLight) * m_SpotLightsUB.Count);

			m_SpotShadowUB.Count = 0;
			for (uint32_t i = 0; i < m_SpotLightsUB.Count; i++)
			{
				auto& light = m_SpotLightsUB.SpotLights[i];
				if (!light.CastsShadows || m_SpotShadowCount >= MaxSpotShadows)
				{
					light.ShadowIndex = 0;
					light.AtlasOffsetX = 0.0f;
					light.AtlasOffsetY = 0.0f;
					light.AtlasScale = 1.0f;
					if (m_SpotShadowCount >= MaxSpotShadows)
						light.CastsShadows = 0;
					continue;
				}

				const uint32_t atlasIndex = m_SpotShadowCount++;
				const uint32_t tileX = atlasIndex % m_SpotShadowAtlasGridSize;
				const uint32_t tileY = atlasIndex / m_SpotShadowAtlasGridSize;
				const float atlasScale = 1.0f / (float)m_SpotShadowAtlasGridSize;

				light.ShadowIndex = atlasIndex;
				light.AtlasOffsetX = tileX * atlasScale;
				light.AtlasOffsetY = tileY * atlasScale;
				light.AtlasScale = atlasScale;

				const glm::vec3 direction = glm::normalize(light.Direction);
				const glm::vec3 up = glm::abs(glm::dot(direction, glm::vec3(0, 1, 0))) < 0.99f
					? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
				const glm::mat4 view = glm::lookAt(light.Position, light.Position + direction, up);
				const float nearPlane = 0.1f;
				const float farPlane = glm::max(0.1f, light.Range);
				const float fov = glm::radians(glm::clamp(light.Angle, 1.0f, 179.0f));
				const glm::mat4 proj = glm::perspective(fov, 1.0f, nearPlane, farPlane);
				m_SpotShadowUB.ViewProjection[atlasIndex] = proj * view;
			}

			m_SpotShadowUB.Count = m_SpotShadowCount;

			auto slData = m_SpotLightsUB;
			uint32_t slSize = (uint32_t)(16ull + sizeof(SpotLight) * slData.Count);
			auto spotShadowData = m_SpotShadowUB;
			uint32_t spotShadowSize = (uint32_t)(sizeof(glm::mat4) * MaxSpotShadows + 16ull);
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, slData, slSize]() mutable {
				instance->m_UBSSpotLights->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &slData, slSize);
				});

			Renderer::Submit([instance, spotShadowData, spotShadowSize]() mutable {
				instance->m_UBSSpotShadow->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &spotShadowData, spotShadowSize);
				});
		}

		// ── Directional shadow matrix ─────────────────────────────────────────
		// Single ortho shadow map centred on the camera position.
		{
			const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];

			if (dirLight.Intensity > 0.0f && dirLight.CastShadows)
			{
				const glm::vec3 lightDir = glm::normalize(dirLight.Direction);
				const glm::vec3 camPos = glm::vec3(glm::inverse(camera.ViewMatrix)[3]);

				// Pick an up vector that is not collinear with the light direction
				const glm::vec3 up = glm::abs(glm::dot(lightDir, glm::vec3(0, 1, 0))) < 0.99f
					? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);

				const glm::mat4 lightView = glm::lookAt(
					camPos - lightDir * 150.0f,
					camPos,
					up);

				const float halfSize = m_Options.MaxShadowDistance * 0.5f;
				const glm::mat4 lightProj = glm::ortho(
					-halfSize, halfSize,
					-halfSize, halfSize,
					-500.0f, 500.0f);

				m_ShadowUB.ViewProjection[0] = lightProj * lightView;
			}
			else
			{
				m_ShadowUB.ViewProjection[0] = glm::mat4(1.0f);
			}

			auto shadowData = m_ShadowUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, shadowData]() mutable {
				instance->m_UBSShadow->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &shadowData, sizeof(UBShadow));
				});
		}

		// ── Renderer data uniform buffer ──────────────────────────────────────
		{
			const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
			m_RendererDataUB.SoftShadows = m_Options.SoftShadows && dirLight.SoftShadows;
			m_RendererDataUB.LightSize = dirLight.LightSize;
			m_RendererDataUB.MaxShadowDistance = m_Options.MaxShadowDistance;
			m_RendererDataUB.ShadowFade = m_Options.ShadowFade;
			m_RendererDataUB.CascadeSplits = glm::vec4(-1000000.0f);
			m_RendererDataUB.TilesCountX = m_LightTilesCountX;

			auto rdData = m_RendererDataUB;
			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, rdData]() mutable {
				instance->m_UBSRendererData->RT_Get()->RT_SetData(
					instance->m_UploadCommandBuffer, &rdData, sizeof(UBRendererData));
				});
		}

		// ── Update environment texture bindings in geometry passes ────────────
		Ref<TextureCube> radianceMap = GetEnvironmentRadianceMap(m_SceneData.SceneEnvironment);
		Ref<TextureCube> irradianceMap = GetEnvironmentIrradianceMap(m_SceneData.SceneEnvironment);
		m_GeometryPass->SetInput("u_EnvRadianceTex", radianceMap);
		m_GeometryPass->SetInput("u_EnvIrradianceTex", irradianceMap);
		m_GeometryPassTransparent->SetInput("u_EnvRadianceTex", radianceMap);
		m_GeometryPassTransparent->SetInput("u_EnvIrradianceTex", irradianceMap);

		m_UploadCommandBuffer->End();
		m_UploadCommandBuffer->Submit();
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Mesh submission
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::SubmitStaticMesh(
		Ref<StaticMesh>    staticMesh,
		Ref<MeshSource>    meshSource,
		Ref<MaterialTable> materialTable,
		const glm::mat4& transform,
		Ref<Material>      overrideMaterial)
	{
		SubmitStaticMeshInternal(staticMesh, meshSource, materialTable, transform, overrideMaterial, false);
	}

	void SceneRenderer::SubmitSelectedStaticMesh(
		Ref<StaticMesh>    staticMesh,
		Ref<MeshSource>    meshSource,
		Ref<MaterialTable> materialTable,
		const glm::mat4& transform,
		Ref<Material>      overrideMaterial)
	{
		SubmitStaticMeshInternal(staticMesh, meshSource, materialTable, transform, overrideMaterial, true);
	}

	void SceneRenderer::SubmitStaticMeshInternal(
		Ref<StaticMesh>    staticMesh,
		Ref<MeshSource>    meshSource,
		Ref<MaterialTable> materialTable,
		const glm::mat4& transform,
		Ref<Material>      overrideMaterial,
		bool               isSelected)
	{
		LUX_CORE_ASSERT(m_Active, "SubmitStaticMesh called outside BeginScene/EndScene");

		const auto& submeshData = meshSource->GetSubmeshes();

		for (uint32_t submeshIndex : staticMesh->GetSubmeshes())
		{
			const auto& submesh = submeshData[submeshIndex];

			// Combine the entity transform with the submesh local transform
			const glm::mat4 submeshTransform = transform * submesh.Transform;

			// Resolve material handle
			AssetHandle materialHandle = 0;
			Ref<MaterialAsset> materialAsset;
			Ref<Material> resolvedOverrideMaterial = overrideMaterial;

			if (!resolvedOverrideMaterial)
			{
				materialHandle = ResolveStaticMeshMaterialHandle(materialTable, staticMesh, meshSource, submesh.MaterialIndex);

				if (materialHandle)
					materialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);

				if (!materialAsset)
					resolvedOverrideMaterial = Renderer::GetDefaultWhiteMaterial();
			}

			LUX_CORE_ASSERT(resolvedOverrideMaterial || materialAsset, "No material found for submesh {}", submeshIndex);

			const AssetHandle keyMaterialHandle = resolvedOverrideMaterial
				? AssetHandle((uint64_t)resolvedOverrideMaterial.Raw())
				: materialHandle;
			const MeshKey key{ GetStaticMeshKeyHandle(staticMesh), keyMaterialHandle, submeshIndex, isSelected };

			// ── Store transform ───────────────────────────────────────────────
			const uint32_t transformIndex = (uint32_t)m_TransformData.size();
			auto& td = m_TransformData.emplace_back();
			td.MRow[0] = { submeshTransform[0][0], submeshTransform[1][0], submeshTransform[2][0], submeshTransform[3][0] };
			td.MRow[1] = { submeshTransform[0][1], submeshTransform[1][1], submeshTransform[2][1], submeshTransform[3][1] };
			td.MRow[2] = { submeshTransform[0][2], submeshTransform[1][2], submeshTransform[2][2], submeshTransform[3][2] };

			// ── Main draw list ────────────────────────────────────────────────
			m_MeshTransformMap[key].ObjectIndices.push_back(transformIndex);

			const bool isTransparent = materialAsset ? materialAsset->IsTransparent() : false;
			auto& destList = isTransparent ? m_TransparentStaticMeshDrawList : m_StaticMeshDrawList;
			auto& dc = destList[key];
			dc.StaticMesh = staticMesh;
			dc.MeshSource = meshSource;
			dc.SubmeshIndex = submeshIndex;
			dc.MaterialHandle = materialHandle;
			dc.MaterialTable = materialTable;
			dc.OverrideMaterial = resolvedOverrideMaterial;
			dc.InstanceCount++;

			// ── Selected list ─────────────────────────────────────────────────
			if (isSelected)
			{
				auto& selDc = m_SelectedStaticMeshDrawList[key];
				selDc.StaticMesh = staticMesh;
				selDc.MeshSource = meshSource;
				selDc.SubmeshIndex = submeshIndex;
				selDc.MaterialHandle = materialHandle;
				selDc.MaterialTable = materialTable;
				selDc.OverrideMaterial = resolvedOverrideMaterial;
				selDc.InstanceCount++;
			}

			// ── Shadow pass list ──────────────────────────────────────────────
			const bool castsShadows = resolvedOverrideMaterial
				? true // override materials always cast shadows
				: (materialAsset && materialAsset->IsShadowCasting());

			if (castsShadows)
			{
				m_ShadowMeshTransformMap[key].Cascade.ObjectIndices.push_back(transformIndex);

				auto& shadowDc = m_StaticMeshShadowPassDrawList[key];
				shadowDc.StaticMesh = staticMesh;
				shadowDc.MeshSource = meshSource;
				shadowDc.SubmeshIndex = submeshIndex;
				shadowDc.MaterialHandle = materialHandle;
				shadowDc.MaterialTable = materialTable;
				shadowDc.OverrideMaterial = resolvedOverrideMaterial;
				shadowDc.InstanceCount++;
			}
		}
	}

	void SceneRenderer::SubmitPhysicsStaticDebugMesh(Ref<StaticMesh> staticMesh,
		Ref<MeshSource> meshSource,
		const glm::mat4& transform,
		bool isSimpleCollider)
	{
		SubmitStaticDebugMesh(m_StaticColliderDrawList, staticMesh, meshSource, transform,
			isSimpleCollider ? m_SimpleColliderMaterial : m_ComplexColliderMaterial);
	}

	void SceneRenderer::SubmitStaticDebugMesh(std::map<MeshKey, StaticDrawCommand>& drawList,
		Ref<StaticMesh>  staticMesh,
		Ref<MeshSource>  meshSource,
		const glm::mat4& transform,
		Ref<Material>    material)
	{
		const auto& submeshData = meshSource->GetSubmeshes();

		for (uint32_t submeshIndex : staticMesh->GetSubmeshes())
		{
			const glm::mat4 submeshTransform = transform * submeshData[submeshIndex].Transform;

			// Use the material pointer as a fake asset handle so each material gets its own MeshKey bucket
			const AssetHandle fakeHandle = (AssetHandle)(uint64_t)material.Raw();
			const MeshKey key{ GetStaticMeshKeyHandle(staticMesh), fakeHandle, submeshIndex, false };

			const uint32_t transformIndex = (uint32_t)m_TransformData.size();
			m_MeshTransformMap[key].ObjectIndices.push_back(transformIndex);

			auto& td = m_TransformData.emplace_back();
			td.MRow[0] = { submeshTransform[0][0], submeshTransform[1][0], submeshTransform[2][0], submeshTransform[3][0] };
			td.MRow[1] = { submeshTransform[0][1], submeshTransform[1][1], submeshTransform[2][1], submeshTransform[3][1] };
			td.MRow[2] = { submeshTransform[0][2], submeshTransform[1][2], submeshTransform[2][2], submeshTransform[3][2] };

			auto& dc = drawList[key];
			dc.StaticMesh = staticMesh;
			dc.MeshSource = meshSource;
			dc.SubmeshIndex = submeshIndex;
			dc.MaterialHandle = fakeHandle;
			dc.OverrideMaterial = material;
			dc.InstanceCount++;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// EndScene → FlushDrawList
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::EndScene()
	{
		LUX_CORE_ASSERT(m_Active);
		FlushDrawList();
		m_Active = false;
	}

	void SceneRenderer::WaitForThreads()
	{
		AssetManager::SyncWithAssetThread();
	}

	void SceneRenderer::FlushDrawList()
	{
		// Clear lists and bail if GPU resources not ready
		auto clearAll = [this]()
			{
				m_TransformData.clear();
				m_MeshTransformMap.clear();
				m_ShadowMeshTransformMap.clear();
				m_StaticMeshDrawList.clear();
				m_TransparentStaticMeshDrawList.clear();
				m_SelectedStaticMeshDrawList.clear();
				m_StaticMeshShadowPassDrawList.clear();
				m_StaticColliderDrawList.clear();
			};

		if (!m_ResourcesCreated)
		{
			clearAll();
			return;
		}

		// ── 1. Build the flat ObjectIndex array and assign base offsets ────────
		// Each MeshKey in m_MeshTransformMap gets a contiguous block of object indices.
		// The shader uses  objectIndex = ObjectIndexBase + gl_InstanceIndex
		// to look up its row in the InstanceTransforms SSBO.
		uint32_t cursor = 0;
		std::vector<uint32_t> objectIndexData;
		for (auto& [key, tmd] : m_MeshTransformMap)
		{
			tmd.ObjectIndexBase = cursor;
			for (uint32_t idx : tmd.ObjectIndices)
				objectIndexData.push_back(idx * 3u);
			cursor += (uint32_t)tmd.ObjectIndices.size();
		}
		// Do the same for the shadow-specific transform map
		for (auto& [key, shadowTmd] : m_ShadowMeshTransformMap)
		{
			shadowTmd.Cascade.ObjectIndexBase = cursor;
			for (uint32_t idx : shadowTmd.Cascade.ObjectIndices)
				objectIndexData.push_back(idx * 3u);
			cursor += (uint32_t)shadowTmd.Cascade.ObjectIndices.size();
		}

		// ── 2. Upload InstanceTransforms and ObjectIndexes SSBOs ──────────────
		m_UploadCommandBuffer->Begin();

		if (!m_TransformData.empty())
		{
			const auto transformData = m_TransformData;
			const auto indexData = objectIndexData;
			Ref<SceneRenderer> instance = this;

			Renderer::Submit([instance, transformData, indexData]() mutable {

				Ref<RenderCommandBuffer> cmd = instance->m_UploadCommandBuffer;

				// Grow InstanceTransforms SSBO if needed
				const uint32_t transformBytes = (uint32_t)(sizeof(TransformVertexData) * transformData.size());
				if (instance->m_SBSInstanceTransforms->RT_Get()->GetHandle()->getDesc().byteSize < transformBytes)
					instance->m_SBSInstanceTransforms->Resize(transformBytes * 2u);
				instance->m_SBSInstanceTransforms->RT_Get()->RT_SetData(cmd, transformData.data(), transformBytes);

				// Grow ObjectIndexes SSBO if needed
				if (!indexData.empty())
				{
					const uint32_t indexBytes = (uint32_t)(sizeof(uint32_t) * indexData.size());
					if (instance->m_SBSObjectIndexes->RT_Get()->GetHandle()->getDesc().byteSize < indexBytes)
						instance->m_SBSObjectIndexes->Resize(indexBytes * 2u);
					instance->m_SBSObjectIndexes->RT_Get()->RT_SetData(cmd, indexData.data(), indexBytes);
				}
				});
		}

		m_UploadCommandBuffer->End();
		m_UploadCommandBuffer->Submit();

		// ── 3. Execute render passes ──────────────────────────────────────────
		m_CommandBuffer->Begin();

		ShadowMapPass();
		SpotShadowMapPass();
		PreDepthPass();
		HZBCompute();
		PreIntegration();
		LightCullingPass();
		SkyboxPass();
		GeometryPass();
		if (m_Options.EnableGTAO)
		{
			GTAOCompute();
			GTAODenoiseCompute();
			AOComposite();
		}
		PreConvolutionCompute();
		if (m_Options.EnableSSR)
		{
			SSRCompute();
			SSRCompositePass();
		}
		if (m_Options.EnableJumpFlood && !m_SelectedStaticMeshDrawList.empty())
			JumpFloodPass();
		BloomCompute();
		CompositePass();
		if (m_Options.EnableJumpFlood && !m_SelectedStaticMeshDrawList.empty())
			JumpFloodCompositePass();

		if (m_Options.ShowGrid)
			GridPass();

		// ── 4. Renderer2D (lines, collider outlines, debug renderer queue) ────
		{
			const auto& sceneCamera = m_SceneData.SceneCamera;
			const glm::mat4 viewProj = sceneCamera.Camera.GetProjectionMatrix() * sceneCamera.ViewMatrix;

			m_Renderer2D->SetTargetFramebuffer(m_CompositingFramebuffer);
			m_Renderer2D->BeginScene(viewProj, sceneCamera.ViewMatrix);

			// Flush any queued DebugRenderer work
			for (auto& fn : m_DebugRenderer->GetRenderQueue())
				fn(m_Renderer2D);
			m_DebugRenderer->ClearRenderQueue();

			// Physics collider outlines (2D wireframe lines)
			if (m_Options.ShowPhysicsColliders)
			{
				// Future: iterate m_StaticColliderDrawList and draw via Renderer2D AABB
				// For now the 3D collider meshes are drawn in GeometryPass via m_StaticColliderDrawList.
			}

			m_Renderer2D->EndScene();
		}

		if (m_DOFSettings.Enabled)
			DOFPass();

		m_CommandBuffer->End();
		m_CommandBuffer->Submit();

		// ── 5. Update statistics ──────────────────────────────────────────────
		UpdateStatistics();

		// ── 6. Clear draw lists for next frame ────────────────────────────────
		clearAll();
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Individual pass implementations
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::ShadowMapPass()
	{
		const auto& dirLight = m_SceneData.SceneLightEnvironment.DirectionalLights[0];
		if (dirLight.Intensity <= 0.0f || !dirLight.CastShadows)
		{
			// Clear the shadow map so geometry doesn't sample stale data
			Renderer::BeginRenderPass(m_CommandBuffer, m_ShadowMapPass, /*explicitClear=*/true);
			Renderer::EndRenderPass(m_CommandBuffer);
			return;
		}

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "ShadowMapPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_ShadowMapPass, /*explicitClear=*/true);

		for (auto& [key, dc] : m_StaticMeshShadowPassDrawList)
		{
			auto it = m_ShadowMeshTransformMap.find(key);
			if (it == m_ShadowMeshTransformMap.end()) continue;

			const auto& cascadeTmd = it->second.Cascade;
			const uint32_t instCount = (uint32_t)cascadeTmd.ObjectIndices.size();
			if (instCount == 0) continue;

			StaticDrawCommand drawCmd = dc;
			drawCmd.InstanceCount = instCount;

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, cascadeTmd]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, cascadeTmd, /*bindMaterial=*/false);
				});
		}

		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SpotShadowMapPass()
	{
		if (m_SpotShadowCount == 0)
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_SpotShadowMapPass, /*explicitClear=*/true);
			Renderer::EndRenderPass(m_CommandBuffer);
			return;
		}

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "SpotShadowMapPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_SpotShadowMapPass, /*explicitClear=*/true);

		const uint32_t tilesPerRow = m_SpotShadowAtlasGridSize;
		const uint32_t tileSize = m_SpotShadowTileSize;
		const uint32_t atlasSize = m_SpotShadowMapSize;

		for (uint32_t shadowIndex = 0; shadowIndex < m_SpotShadowCount; shadowIndex++)
		{
			const uint32_t tileX = shadowIndex % tilesPerRow;
			const uint32_t tileY = shadowIndex / tilesPerRow;
			Renderer::SetViewport(m_CommandBuffer, tileX * tileSize, tileY * tileSize, tileSize, tileSize);

			for (auto& [key, dc] : m_StaticMeshShadowPassDrawList)
			{
				auto it = m_ShadowMeshTransformMap.find(key);
				if (it == m_ShadowMeshTransformMap.end()) continue;
				const auto& cascadeTmd = it->second.Cascade;
				const uint32_t instCount = (uint32_t)cascadeTmd.ObjectIndices.size();
				if (instCount == 0) continue;

				StaticDrawCommand drawCmd = dc;
				drawCmd.InstanceCount = instCount;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, cascadeTmd, shadowIndex]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, cascadeTmd, /*bindMaterial=*/false, shadowIndex);
					});
			}
		}

		Renderer::SetViewport(m_CommandBuffer, 0, 0, atlasSize, atlasSize);

		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::PreDepthPass()
	{
		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "PreDepthPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_PreDepthPass, /*explicitClear=*/true);

		for (auto& [key, dc] : m_StaticMeshDrawList)
		{
			auto it = m_MeshTransformMap.find(key);
			if (it == m_MeshTransformMap.end()) continue;

			const auto& tmd = it->second;
			StaticDrawCommand drawCmd = dc;

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, tmd]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/false);
				});
		}

		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::HZBCompute()
	{
		if (!m_HierarchicalDepthPass || !m_HierarchicalDepthTexture.Texture || !m_PreDepthPass || m_HZBMaterials.empty())
			return;

		constexpr uint32_t maxMipBatchSize = 4;
		const uint32_t hzbMipCount = m_HierarchicalDepthTexture.Texture->GetMipLevelCount();
		if (hzbMipCount == 0)
			return;

		struct HierarchicalZComputePushConstants
		{
			glm::vec2 DispatchThreadIdToBufferUV;
			glm::vec2 InputViewportMaxBound;
			glm::vec2 InvSize;
			int FirstLod = 0;
			int IsFirstPass = 0;
		};

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "HZB");
		Renderer::BeginComputePass(m_CommandBuffer, m_HierarchicalDepthPass);

		auto reduceHZB = [&](uint32_t startDestMip, uint32_t parentMip, const glm::vec2& dispatchThreadIdToBufferUV, const glm::vec2& inputViewportMaxBound, bool isFirstPass)
		{
			const uint32_t materialIndex = startDestMip / maxMipBatchSize;
			if (materialIndex >= m_HZBMaterials.size() || !m_HZBMaterials[materialIndex])
				return;

			const glm::uvec2 srcSize = DivideRoundUp(m_HierarchicalDepthTexture.Texture->GetSize(), 1u << glm::min(parentMip, 31u));
			const glm::uvec2 dstSize = DivideRoundUp(m_HierarchicalDepthTexture.Texture->GetSize(), 1u << glm::min(startDestMip, 31u));

			HierarchicalZComputePushConstants pushConstants;
			pushConstants.DispatchThreadIdToBufferUV = dispatchThreadIdToBufferUV;
			pushConstants.InputViewportMaxBound = inputViewportMaxBound;
			pushConstants.InvSize = {
				srcSize.x > 0 ? 1.0f / (float)srcSize.x : 1.0f,
				srcSize.y > 0 ? 1.0f / (float)srcSize.y : 1.0f
			};
			pushConstants.FirstLod = (int)startDestMip;
			pushConstants.IsFirstPass = isFirstPass ? 1 : 0;

			const glm::uvec3 workGroups = {
				DivideRoundUp(glm::max(1u, dstSize.x), 8u),
				DivideRoundUp(glm::max(1u, dstSize.y), 8u),
				1
			};

			Renderer::DispatchCompute(m_CommandBuffer, m_HierarchicalDepthPass, m_HZBMaterials[materialIndex], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			m_HierarchicalDepthPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_HierarchicalDepthTexture.Texture->GetImage(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		};

		const glm::uvec2 depthSize = m_PreDepthPass->GetDepthOutput()->GetSize();
		if (depthSize.x > 0 && depthSize.y > 0)
		{
			reduceHZB(
				0,
				0,
				1.0f / glm::vec2(depthSize),
				(glm::vec2(depthSize) - 0.5f) / glm::vec2(depthSize),
				true);
		}

		for (uint32_t startDestMip = maxMipBatchSize; startDestMip < hzbMipCount; startDestMip += maxMipBatchSize)
		{
			const glm::uvec2 parentSize = DivideRoundUp(m_HierarchicalDepthTexture.Texture->GetSize(), 1u << glm::min(startDestMip - 1u, 31u));
			if (parentSize.x == 0 || parentSize.y == 0)
				continue;

			reduceHZB(
				startDestMip,
				startDestMip - 1u,
				2.0f / glm::vec2(parentSize),
				glm::vec2(1.0f),
				false);
		}

		Renderer::EndComputePass(m_CommandBuffer, m_HierarchicalDepthPass);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::PreIntegration()
	{
		if (!m_PreIntegrationPass || !m_PreIntegrationVisibilityTexture.Texture || m_PreIntegrationMaterials.empty())
			return;

		Ref<Texture2D> visibilityTexture = m_PreIntegrationVisibilityTexture.Texture;
		const uint32_t mipCount = visibilityTexture->GetMipLevelCount();
		if (mipCount < 2)
			return;

		Ref<Image2D> visibilityImage = visibilityTexture->GetImage();
		Renderer::ClearImage(m_CommandBuffer, visibilityImage, nvrhi::Color(1.0f, 1.0f, 1.0f, 1.0f), visibilityImage->GetMipImageView(0));

		struct PreIntegrationComputePushConstants
		{
			glm::vec2 HZBResFactor;
			glm::vec2 ResFactor;
			glm::vec2 ProjectionParams;
			int PrevLod = 0;
		} pushConstants;

		pushConstants.ProjectionParams = { m_SceneData.SceneCamera.Far, m_SceneData.SceneCamera.Near };

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "PreIntegration");
		Renderer::BeginComputePass(m_CommandBuffer, m_PreIntegrationPass);

		for (uint32_t mip = 1; mip < mipCount && mip - 1 < m_PreIntegrationMaterials.size(); mip++)
		{
			auto [mipWidth, mipHeight] = visibilityTexture->GetMipSize(mip);
			if (mipWidth == 0 || mipHeight == 0 || !m_PreIntegrationMaterials[mip - 1])
				continue;

			const glm::vec2 resFactor = 1.0f / glm::vec2(mipWidth, mipHeight);
			pushConstants.HZBResFactor = resFactor * m_SSROptions.HZBUvFactor;
			pushConstants.ResFactor = resFactor;
			pushConstants.PrevLod = (int)mip - 1;

			const glm::uvec3 workGroups = { DivideRoundUp(mipWidth, 8u), DivideRoundUp(mipHeight, 8u), 1 };
			Renderer::DispatchCompute(m_CommandBuffer, m_PreIntegrationPass, m_PreIntegrationMaterials[mip - 1], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			m_PreIntegrationPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, visibilityImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		}

		Renderer::EndComputePass(m_CommandBuffer, m_PreIntegrationPass);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::LightCullingPass()
	{
		if (!m_LightCullingPass || m_ViewportWidth == 0 || m_ViewportHeight == 0)
			return;

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "LightCullingPass");
		Renderer::LightCulling(m_CommandBuffer, m_LightCullingPass, nullptr, { m_LightTilesCountX, m_LightTilesCountY, 1 });
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SkyboxPass()
	{
		Ref<TextureCube> radianceMap = GetEnvironmentRadianceMap(m_SceneData.SceneEnvironment);
		if (!radianceMap)
			return;

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "SkyboxPass");

		m_SkyboxMaterial->Set("u_Uniforms.TextureLod", m_SceneData.SkyboxLod);
		m_SkyboxMaterial->Set("u_Uniforms.Intensity", m_SceneData.SceneEnvironmentIntensity);
		m_SkyboxMaterial->Set("u_Texture", radianceMap);

		Renderer::BeginRenderPass(m_CommandBuffer, m_SkyboxPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_SkyboxPass->GetPipeline(), m_SkyboxMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GeometryPass()
	{
		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "GeometryPass");

		// Selected geometry mask, matching Hazel's static selected path. Lux does
		// not run animation or jump-flood outline passes here.
		if (!m_SelectedStaticMeshDrawList.empty())
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_SelectedGeometryPass);

			for (auto& [key, dc] : m_SelectedStaticMeshDrawList)
			{
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = dc;
				drawCmd.OverrideMaterial = m_SelectedGeometryMaterial;
				const auto& tmd = it->second;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, tmd]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true);
					});
			}

			Renderer::EndRenderPass(m_CommandBuffer);
		}

		// ── Opaque geometry ───────────────────────────────────────────────────
		Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryPass);

		for (auto& [key, dc] : m_StaticMeshDrawList)
		{
			auto it = m_MeshTransformMap.find(key);
			if (it == m_MeshTransformMap.end()) continue;

			StaticDrawCommand drawCmd = dc;
			const auto& tmd = it->second;

			Ref<SceneRenderer> instance = this;
			Renderer::Submit([instance, drawCmd, tmd]() mutable {
				instance->RT_DrawStaticMesh(
					instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true);
				});
		}

		// Physics debug meshes drawn in the opaque geometry pass
		if (m_Options.ShowPhysicsColliders)
		{
			for (auto& [key, dc] : m_StaticColliderDrawList)
			{
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = dc;
				const auto& tmd = it->second;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, tmd]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true);
					});
			}
		}

		Renderer::EndRenderPass(m_CommandBuffer);

		// ── Transparent geometry ──────────────────────────────────────────────
		if (!m_TransparentStaticMeshDrawList.empty())
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryPassTransparent);

			for (auto& [key, dc] : m_TransparentStaticMeshDrawList)
			{
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = dc;
				const auto& tmd = it->second;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, tmd]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true);
					});
			}

			Renderer::EndRenderPass(m_CommandBuffer);
		}

		// ── Selected wireframe overlay ────────────────────────────────────────
		if (m_Options.ShowSelectedInWireframe && !m_SelectedStaticMeshDrawList.empty())
		{
			Renderer::BeginRenderPass(m_CommandBuffer, m_GeometryWireframePass);

			for (auto& [key, dc] : m_SelectedStaticMeshDrawList)
			{
				auto it = m_MeshTransformMap.find(key);
				if (it == m_MeshTransformMap.end()) continue;

				StaticDrawCommand drawCmd = dc;
				drawCmd.OverrideMaterial = m_WireframeMaterial;
				const auto& tmd = it->second;

				Ref<SceneRenderer> instance = this;
				Renderer::Submit([instance, drawCmd, tmd]() mutable {
					instance->RT_DrawStaticMesh(
						instance->m_CommandBuffer, drawCmd, tmd, /*bindMaterial=*/true);
					});
			}

			Renderer::EndRenderPass(m_CommandBuffer);
		}

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GTAOCompute()
	{
		if (!m_Options.EnableGTAO || !m_GTAOComputePass || !m_GTAOOutputImage)
			return;

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "GTAO");
		Renderer::BeginComputePass(m_CommandBuffer, m_GTAOComputePass);
		Renderer::DispatchCompute(m_CommandBuffer, m_GTAOComputePass, nullptr, m_GTAOWorkGroups, Buffer(&m_GTAODataCB, sizeof(m_GTAODataCB)));
		Renderer::EndComputePass(m_CommandBuffer, m_GTAOComputePass);
		m_GTAOComputePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_GTAOOutputImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		m_GTAOComputePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_GTAOEdgesOutputImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GTAODenoiseCompute()
	{
		if (!m_Options.EnableGTAO || !m_GTAODenoisePass[0] || !m_GTAODenoisePass[1] || !m_GTAOOutputImage)
			return;

		if (m_Options.GTAODenoisePasses == 0)
		{
			m_GTAOFinalImage = m_GTAOOutputImage;
			if (m_AOCompositePass)
				m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
				m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);
			return;
		}

		m_GTAODenoiseConstants.DenoiseBlurBeta = m_GTAODataCB.DenoiseBlurBeta;
		m_GTAODenoiseConstants.HalfRes = m_GTAODataCB.HalfRes;

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "GTAO-Denoise");
		for (uint32_t pass = 0; pass < m_Options.GTAODenoisePasses; pass++)
		{
			const uint32_t passIndex = (pass % 2u) != 0u ? 1u : 0u;
			Ref<ComputePass> denoisePass = m_GTAODenoisePass[passIndex];
			Ref<Image2D> outputImage = passIndex == 0 ? m_GTAODenoiseImage : m_GTAOOutputImage;

			Renderer::BeginComputePass(m_CommandBuffer, denoisePass);
			Renderer::DispatchCompute(m_CommandBuffer, denoisePass, nullptr, m_GTAODenoiseWorkGroups, Buffer(&m_GTAODenoiseConstants, sizeof(m_GTAODenoiseConstants)));
			Renderer::EndComputePass(m_CommandBuffer, denoisePass);
			denoisePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, outputImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		}

		m_GTAOFinalImage = (m_Options.GTAODenoisePasses % 2u) != 0u ? m_GTAODenoiseImage : m_GTAOOutputImage;
		if (m_AOCompositePass)
			m_AOCompositePass->SetInput("u_GTAOTex", m_GTAOFinalImage);
		if (m_SSRPass && m_SSRPass->IsInputValid("u_GTAOTex"))
			m_SSRPass->SetInput("u_GTAOTex", m_GTAOFinalImage);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::AOComposite()
	{
		if (!m_AOCompositePass || !m_AOCompositeMaterial || !m_GTAOFinalImage)
			return;

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "AOComposite");
		Renderer::BeginRenderPass(m_CommandBuffer, m_AOCompositePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_AOCompositePass->GetPipeline(), m_AOCompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::PreConvolutionCompute()
	{
		if (!m_Options.EnableSSR || !m_PreConvolutionComputePass || !m_PreConvolutedTexture.Texture || m_PreConvolutionMaterials.empty())
			return;

		struct PreConvolutionComputePushConstants
		{
			int PrevLod = 0;
			int Mode = 0;
		} pushConstants;

		Ref<Image2D> preConvolutedImage = m_PreConvolutedTexture.Texture->GetImage();
		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "PreConvolution");
		Renderer::BeginComputePass(m_CommandBuffer, m_PreConvolutionComputePass);

		if (m_PreConvolutionMaterials[0])
		{
			auto [width, height] = m_PreConvolutedTexture.Texture->GetMipSize(0);
			const glm::uvec3 workGroups = { DivideRoundUp(glm::max(1u, width), 16u), DivideRoundUp(glm::max(1u, height), 16u), 1 };
			pushConstants.PrevLod = 0;
			pushConstants.Mode = 0;
			Renderer::DispatchCompute(m_CommandBuffer, m_PreConvolutionComputePass, m_PreConvolutionMaterials[0], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			m_PreConvolutionComputePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, preConvolutedImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		}

		const uint32_t mipCount = m_PreConvolutedTexture.Texture->GetMipLevelCount();
		for (uint32_t mip = 1; mip < mipCount && mip < m_PreConvolutionMaterials.size(); mip++)
		{
			if (!m_PreConvolutionMaterials[mip])
				continue;

			auto [mipWidth, mipHeight] = m_PreConvolutedTexture.Texture->GetMipSize(mip);
			const glm::uvec3 workGroups = { DivideRoundUp(glm::max(1u, mipWidth), 16u), DivideRoundUp(glm::max(1u, mipHeight), 16u), 1 };
			pushConstants.PrevLod = (int)mip - 1;

			pushConstants.Mode = 1;
			Renderer::DispatchCompute(m_CommandBuffer, m_PreConvolutionComputePass, m_PreConvolutionMaterials[mip], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			m_PreConvolutionComputePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, preConvolutedImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);

			pushConstants.Mode = 2;
			Renderer::DispatchCompute(m_CommandBuffer, m_PreConvolutionComputePass, m_PreConvolutionMaterials[mip], workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
			m_PreConvolutionComputePass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, preConvolutedImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		}

		Renderer::EndComputePass(m_CommandBuffer, m_PreConvolutionComputePass);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SSRCompute()
	{
		if (!m_Options.EnableSSR || !m_SSRPass || !m_SSRImage)
			return;

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "SSR");
		Renderer::BeginComputePass(m_CommandBuffer, m_SSRPass);
		Renderer::DispatchCompute(m_CommandBuffer, m_SSRPass, nullptr, m_SSRWorkGroups, Buffer(&m_SSROptions, sizeof(m_SSROptions)));
		Renderer::EndComputePass(m_CommandBuffer, m_SSRPass);
		m_SSRPass->GetPipeline()->ImageMemoryBarrier(m_CommandBuffer, m_SSRImage, ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::SSRCompositePass()
	{
		if (!m_Options.EnableSSR || !m_SSRCompositePass || !m_SSRCompositeMaterial)
			return;

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "SSRComposite");
		Renderer::BeginRenderPass(m_CommandBuffer, m_SSRCompositePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_SSRCompositePass->GetPipeline(), m_SSRCompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::DOFPass()
	{
		if (!m_DOFSettings.Enabled || !m_DOFPass || !m_DOFMaterial)
			return;

		const float focusDistance = glm::max(0.001f, m_DOFSettings.FocusDistance);
		m_DOFMaterial->Set("u_Uniforms.DOFParams", glm::vec2(focusDistance, m_DOFSettings.BlurSize));

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "DOF");
		Renderer::BeginRenderPass(m_CommandBuffer, m_DOFPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_DOFPass->GetPipeline(), m_DOFMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::JumpFloodPass()
	{
		if (!m_Options.EnableJumpFlood || !m_JumpFloodInitPass || !m_JumpFloodInitMaterial || !m_JumpFloodPasses[0] || !m_JumpFloodPasses[1])
			return;

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "JumpFlood");
		Renderer::BeginRenderPass(m_CommandBuffer, m_JumpFloodInitPass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_JumpFloodInitPass->GetPipeline(), m_JumpFloodInitMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		int step = 4;
		uint32_t passIndex = 0;
		Ref<Image2D> input = m_JumpFloodInitPass->GetOutput(0);
		Ref<Image2D> output = input;

		Ref<Framebuffer> passFramebuffer = m_JumpFloodPasses[0]->GetTargetFramebuffer();
		const glm::vec2 texelSize = {
			passFramebuffer && passFramebuffer->GetWidth() > 0 ? 1.0f / (float)passFramebuffer->GetWidth() : 1.0f,
			passFramebuffer && passFramebuffer->GetHeight() > 0 ? 1.0f / (float)passFramebuffer->GetHeight() : 1.0f
		};

		Buffer vertexOverrides;
		vertexOverrides.Allocate(sizeof(glm::vec2) + sizeof(int));
		vertexOverrides.Write(glm::value_ptr(texelSize), sizeof(glm::vec2));

		while (step > 0)
		{
			Ref<RenderPass> jumpFloodPass = m_JumpFloodPasses[passIndex];
			if (!jumpFloodPass || !m_JumpFloodPassMaterials[passIndex])
				break;

			jumpFloodPass->SetInput("u_Texture", input);
			vertexOverrides.Write(&step, sizeof(int), sizeof(glm::vec2));

			Renderer::BeginRenderPass(m_CommandBuffer, jumpFloodPass);
			Renderer::SubmitFullscreenQuadWithOverrides(m_CommandBuffer, jumpFloodPass->GetPipeline(), m_JumpFloodPassMaterials[passIndex], vertexOverrides, Buffer());
			Renderer::EndRenderPass(m_CommandBuffer);

			output = jumpFloodPass->GetOutput(0);
			input = output;
			passIndex = (passIndex + 1u) % 2u;
			step /= 2;
		}

		vertexOverrides.Release();

		if (m_JumpFloodCompositePass && output)
			m_JumpFloodCompositePass->SetInput("u_Texture", output);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::JumpFloodCompositePass()
	{
		if (!m_Options.EnableJumpFlood || !m_JumpFloodCompositePass || !m_JumpFloodCompositeMaterial)
			return;

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "JumpFloodComposite");
		Renderer::BeginRenderPass(m_CommandBuffer, m_JumpFloodCompositePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_JumpFloodCompositePass->GetPipeline(), m_JumpFloodCompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::BloomCompute()
	{
		if (!m_BloomSettings.Enabled || !m_BloomComputePass || !m_BloomComputePipeline || !m_BloomComputeMaterials.PrefilterMaterial)
			return;

		const uint32_t mipCount = m_BloomComputeTextures[0].Texture->GetMipLevelCount();
		if (mipCount < 4)
			return;

		const uint32_t mips = mipCount - 2;
		if (mips < 3 || !m_BloomComputeMaterials.FirstUpsampleMaterial)
			return;

		struct BloomComputePushConstants
		{
			glm::vec4 Params;
			glm::vec4 TexSize;
			float LOD = 0.0f;
			int Mode = 0;
		} pushConstants;

		const float knee = glm::max(m_BloomSettings.Knee, 0.0001f);
		pushConstants.Params = {
			m_BloomSettings.Threshold,
			m_BloomSettings.Threshold - knee,
			knee * 2.0f,
			0.25f / knee
		};

		auto setTexSize = [&](uint32_t mip)
		{
			auto [mipWidth, mipHeight] = m_BloomComputeTextures[0].Texture->GetMipSize(mip);
			pushConstants.TexSize = {
				(float)mipWidth,
				(float)mipHeight,
				mipWidth > 0 ? 1.0f / (float)mipWidth : 0.0f,
				mipHeight > 0 ? 1.0f / (float)mipHeight : 0.0f
			};
		};

		auto dispatchForMip = [&](Ref<Material> material, uint32_t mip)
		{
			if (!material)
				return;

			auto [mipWidth, mipHeight] = m_BloomComputeTextures[0].Texture->GetMipSize(mip);
			const glm::uvec3 workGroups = {
				AlignUp(glm::max(1u, mipWidth), m_BloomComputeWorkgroupSize) / m_BloomComputeWorkgroupSize,
				AlignUp(glm::max(1u, mipHeight), m_BloomComputeWorkgroupSize) / m_BloomComputeWorkgroupSize,
				1
			};
			Renderer::DispatchCompute(m_CommandBuffer, m_BloomComputePass, material, workGroups, Buffer(&pushConstants, sizeof(pushConstants)));
		};

		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "BloomCompute");
		Renderer::BeginComputePass(m_CommandBuffer, m_BloomComputePass);

		// Prefilter
		pushConstants.Mode = 0;
		pushConstants.LOD = 0.0f;
		setTexSize(0);
		dispatchForMip(m_BloomComputeMaterials.PrefilterMaterial, 0);
		m_BloomComputePipeline->ImageMemoryBarrier(m_CommandBuffer, m_BloomComputeTextures[0].Texture->GetImage(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);

		// Downsample, ping-ponging between texture 0 and texture 1.
		pushConstants.Mode = 1;
		for (uint32_t i = 1; i < mips; i++)
		{
			setTexSize(i);
			pushConstants.LOD = (float)i - 1.0f;
			dispatchForMip(m_BloomComputeMaterials.DownsampleAMaterials[i], i);
			m_BloomComputePipeline->ImageMemoryBarrier(m_CommandBuffer, m_BloomComputeTextures[1].Texture->GetImage(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);

			pushConstants.LOD = (float)i;
			dispatchForMip(m_BloomComputeMaterials.DownsampleBMaterials[i], i);
			m_BloomComputePipeline->ImageMemoryBarrier(m_CommandBuffer, m_BloomComputeTextures[0].Texture->GetImage(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		}

		// First upsample from the smallest downsampled mip.
		pushConstants.Mode = 2;
		pushConstants.LOD = (float)mips - 2.0f;
		setTexSize(mips - 1);
		dispatchForMip(m_BloomComputeMaterials.FirstUpsampleMaterial, mips - 2);
		m_BloomComputePipeline->ImageMemoryBarrier(m_CommandBuffer, m_BloomComputeTextures[2].Texture->GetImage(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);

		// Upsample back to mip 0.
		pushConstants.Mode = 3;
		for (int32_t mip = (int32_t)mips - 3; mip >= 0; mip--)
		{
			pushConstants.LOD = (float)mip;
			setTexSize((uint32_t)mip + 1u);
			dispatchForMip(m_BloomComputeMaterials.UpsampleMaterials[mip], (uint32_t)mip);
			m_BloomComputePipeline->ImageMemoryBarrier(m_CommandBuffer, m_BloomComputeTextures[2].Texture->GetImage(), ResourceAccessFlags::ShaderWrite, ResourceAccessFlags::ShaderRead);
		}

		Renderer::EndComputePass(m_CommandBuffer, m_BloomComputePass);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::CompositePass()
	{
		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "CompositePass");

		m_CompositeMaterial->Set("u_Uniforms.Exposure", m_SceneData.SceneCamera.Camera.GetExposure());
		m_CompositeMaterial->Set("u_Uniforms.BloomIntensity", m_BloomSettings.Enabled ? m_BloomSettings.Intensity : 0.0f);
		m_CompositeMaterial->Set("u_Uniforms.BloomDirtIntensity", m_BloomSettings.Enabled ? m_BloomSettings.DirtIntensity : 0.0f);
		m_CompositeMaterial->Set("u_Uniforms.Opacity", m_Opacity);
		m_CompositeMaterial->Set("u_Uniforms.Time", Application::Get().GetTime());

		Renderer::BeginRenderPass(m_CommandBuffer, m_CompositePass);
		Renderer::SubmitFullscreenQuad(m_CommandBuffer, m_CompositePass->GetPipeline(), m_CompositeMaterial);
		Renderer::EndRenderPass(m_CommandBuffer);

		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::GridPass()
	{
		Renderer::BeginGPUPerfMarker(m_CommandBuffer, "GridPass");
		Renderer::BeginRenderPass(m_CommandBuffer, m_GridRenderPass);
		Renderer::RenderQuad(m_CommandBuffer, m_GridRenderPass->GetPipeline(), m_GridMaterial, glm::mat4(1.0f));
		Renderer::EndRenderPass(m_CommandBuffer);
		Renderer::EndGPUPerfMarker(m_CommandBuffer);
	}

	void SceneRenderer::UpdateStatistics()
	{
		m_Statistics.DrawCalls = 0;
		m_Statistics.Meshes = 0;
		m_Statistics.Instances = 0;

		auto accumulate = [this](const std::map<MeshKey, StaticDrawCommand>& drawList)
			{
				for (const auto& [key, dc] : drawList)
				{
					m_Statistics.DrawCalls++;
					m_Statistics.Meshes++;
					m_Statistics.Instances += dc.InstanceCount;
				}
			};

		accumulate(m_SelectedStaticMeshDrawList);
		accumulate(m_StaticMeshDrawList);
		accumulate(m_TransparentStaticMeshDrawList);

		if (m_Options.ShowPhysicsColliders)
			accumulate(m_StaticColliderDrawList);

		m_Statistics.SavedDraws = m_Statistics.Instances > m_Statistics.DrawCalls
			? m_Statistics.Instances - m_Statistics.DrawCalls
			: 0;

		m_Statistics.TotalGPUTime = m_CommandBuffer->GetExecutionGPUTime(Renderer::GetCurrentFrameIndex());
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Render-thread draw helper
	// Must be called inside a Renderer::Submit() lambda so it executes
	// on the render thread while the command buffer is recording.
	// ─────────────────────────────────────────────────────────────────────────

	void SceneRenderer::RT_DrawStaticMesh(
		Ref<RenderCommandBuffer>  cmd,
		const StaticDrawCommand& dc,
		const TransformMapData& tmd,
		bool                      bindMaterial,
		uint32_t                  lightIndex)
	{
		// Non-const copy of the MeshSource Ref: MeshSource::GetVertexBuffer /
		// GetIndexBuffer / GetMaterials are not marked const, so we cannot call
		// them through a const Ref (which is what we get from a const StaticDrawCommand&).
		Ref<MeshSource> meshSource = dc.MeshSource;

		const auto& submesh = meshSource->GetSubmeshes()[dc.SubmeshIndex];
		nvrhi::GraphicsState& gs = cmd->GetGraphicsState();

		// ── Vertex buffer ─────────────────────────────────────────────────────
		nvrhi::VertexBufferBinding vbb;
		vbb.buffer = meshSource->GetVertexBuffer()->GetHandle();
		vbb.slot = 0;
		vbb.offset = 0;
		gs.vertexBuffers = { vbb };

		// ── Index buffer ──────────────────────────────────────────────────────
		nvrhi::IndexBufferBinding ibb;
		ibb.buffer = meshSource->GetIndexBuffer()->GetHandle();
		ibb.format = nvrhi::Format::R32_UINT;
		ibb.offset = 0;
		gs.indexBuffer = ibb;

		// ── Material (descriptor set 0) ───────────────────────────────────────
		Ref<Material> material;
		if (bindMaterial)
		{
			material = dc.OverrideMaterial;

			if (!material)
			{
				AssetHandle matHandle = dc.MaterialHandle
					? dc.MaterialHandle
					: ResolveStaticMeshMaterialHandle(dc.MaterialTable, dc.StaticMesh, meshSource, submesh.MaterialIndex);

				if (matHandle)
					if (auto matAsset = AssetManager::GetAsset<MaterialAsset>(matHandle))
						material = matAsset->GetMaterial();
			}

			if (!material)
				material = Renderer::GetDefaultWhiteMaterial();

			if (material)
			{
				material->Prepare();
				auto bindingSet = material->GetBindingSet(Renderer::RT_GetCurrentFrameIndex());
				if (bindingSet)
					gs.bindings[0] = bindingSet;
			}
		}

		cmd->RT_CommitGraphicsState();

		// ── Push constants ────────────────────────────────────────────────────
		Buffer materialUniforms = material ? material->GetUniformStorageBuffer() : Buffer();
		const uint64_t pushConstantSize = std::max<uint64_t>(sizeof(MeshDrawPushConstants), materialUniforms.Size);
		std::vector<uint8_t> pushConstants(pushConstantSize);

		if (materialUniforms)
			std::memcpy(pushConstants.data(), materialUniforms.Data, materialUniforms.Size);

		auto& pc = *reinterpret_cast<MeshDrawPushConstants*>(pushConstants.data());
		pc.ObjectIndexBase = tmd.ObjectIndexBase;
		pc.LightIndex = lightIndex;
		pc.BoneTransformBase = 0;
		pc.BoneTransformStride = 0;
		cmd->GetActive()->setPushConstants(pushConstants.data(), pushConstants.size());

		// ── Instanced draw ────────────────────────────────────────────────────
		nvrhi::DrawArguments drawArgs{};
		drawArgs.vertexCount = submesh.IndexCount;
		drawArgs.startIndexLocation = submesh.BaseIndex;
		drawArgs.startVertexLocation = submesh.BaseVertex;
		drawArgs.instanceCount = dc.InstanceCount;
		cmd->GetActive()->drawIndexed(drawArgs);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Output accessors
	// ─────────────────────────────────────────────────────────────────────────

	Ref<Image2D> SceneRenderer::GetFinalPassImage()
	{
		if (m_DOFSettings.Enabled && m_DOFPass)
			return m_DOFPass->GetOutput(0);
		if (m_CompositePass)
			return m_CompositePass->GetOutput(0);
		return nullptr;
	}

	Ref<Pipeline> SceneRenderer::GetFinalPipeline()
	{
		return m_CompositePass ? m_CompositePass->GetPipeline() : nullptr;
	}

	Ref<RenderPass> SceneRenderer::GetFinalRenderPass()
	{
		return m_CompositePass;
	}

	void SceneRenderer::SetLineWidth(float width)
	{
		m_LineWidth = width;
	}

} // namespace Lux
