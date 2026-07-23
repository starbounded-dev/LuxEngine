//
// Note:	this file is to be included in client applications ONLY
//			NEVER include this file anywhere in the engine codebase
//
#pragma once

#include "Lux/Core/Application.h"
#include "Lux/Core/Log.h"
#include "Lux/Core/Input.h"
#include "Lux/Core/Timestep.h"
#include "Lux/Core/Timer.h"
//#include "Lux/Core/Platform.h"
#include "Lux/Core/Version.h"

#include "Lux/Core/Events/Event.h"
#include "Lux/Core/Events/ApplicationEvent.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Events/MouseEvent.h"
//#include "Lux/Core/Events/SceneEvents.h"

//#include "Lux/Core/Math/AABB.h"
//#include "Lux/Core/Math/Ray.h"

#include "imgui/imgui.h"

// --- Hazel Render API ------------------------------
#include "Lux/Renderer/Renderer.h"
//#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Renderer/RenderPass.h"
#include "Lux/Renderer/Framebuffer.h"
#include "Lux/Renderer/VertexBuffer.h"
#include "Lux/Renderer/IndexBuffer.h"
#include "Lux/Renderer/Pipeline.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Renderer/Shader.h"
//#include "Lux/Renderer/Mesh.h"
#include "Lux/Renderer/Camera.h"
#include "Lux/Renderer/Material.h"
// ---------------------------------------------------

// Scenes
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/SceneCamera.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Scene/Components.h"

