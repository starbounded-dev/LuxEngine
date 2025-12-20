//
// Note:	this file is to be included in client applications ONLY
//			NEVER include this file anywhere in the engine codebase
//
#pragma once

#include "StarEngine/Core/Application.h"
#include "StarEngine/Core/Log.h"
#include "StarEngine/Core/Input.h"
#include "StarEngine/Core/TimeStep.h"
#include "StarEngine/Core/Timer.h"
//#include "StarEngine/Core/Platform.h"
#include "StarEngine/Core/Version.h"

#include "StarEngine/Debug/Profiler.h"

#include "StarEngine/Core/Events/Event.h"
#include "StarEngine/Core/Events/ApplicationEvent.h"
#include "StarEngine/Core/Events/KeyEvent.h"
#include "StarEngine/Core/Events/MouseEvent.h"
//#include "StarEngine/Core/Events/SceneEvents.h"

#include "StarEngine/Core/Math/AABB.h"
#include "StarEngine/Core/Math/Ray.h"

#include "imgui/imgui.h"

// --- StarEngine Render API ------------------------------
#include "StarEngine/Renderer/Renderer.h"
//#include "StarEngine/Renderer/SceneRenderer.h"
//#include "StarEngine/Renderer/RenderPass.h"
#include "StarEngine/Renderer/Framebuffer.h"
#include "StarEngine/Renderer/VertexBuffer.h"
#include "StarEngine/Renderer/IndexBuffer.h"
#include "StarEngine/Renderer/Pipeline.h"
#include "StarEngine/Renderer/Texture.h"
#include "StarEngine/Renderer/Shader.h"
//#include "StarEngine/Renderer/Mesh.h"
#include "StarEngine/Renderer/Camera.h"
//#include "StarEngine/Renderer/Material.h"
// ---------------------------------------------------

// Scenes
#include "StarEngine/Scene/Scene.h"
#include "StarEngine/Scene/SceneCamera.h"
#include "StarEngine/Scene/SceneSerializer.h"
#include "StarEngine/Scene/Components.h"
