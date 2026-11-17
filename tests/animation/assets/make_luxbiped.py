# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025-2026 starbounded-dev
#
# Builds LuxBiped, the engine's own rigged and animated test character, and writes it as LuxBiped.fbx
# (binary) and LuxBiped.gltf. The two files are the same model, so the engine's FBX and glTF importers
# can be checked against each other (tests/animation/SkeletalImportTests.cpp).
#
#   blender -b --factory-startup -P make_luxbiped.py -- <output folder>
#
# A 1.75 m, 17-bone biped made of tapered tubes, skinned by smooth distance weights (four bones per
# vertex), with two clips: Idle (2 s) and Walk (1 s, in place).

import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Quaternion, Vector

OUT = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else os.getcwd()

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = 30

# name, parent, head, tail, tube radius at the head. Standing along +Z, facing -Y, left is +X (T-pose).
BONES = [
    ("Hips", None, (0, 0, 0.95), (0, 0, 1.05), 0.15),
    ("Spine", "Hips", (0, 0, 1.05), (0, 0, 1.25), 0.13),
    ("Chest", "Spine", (0, 0, 1.25), (0, 0, 1.45), 0.15),
    ("Neck", "Chest", (0, 0, 1.45), (0, 0, 1.55), 0.05),
    ("Head", "Neck", (0, 0, 1.55), (0, 0, 1.75), 0.10),
]
for side, sign in (("L", 1), ("R", -1)):
    BONES += [
        (f"UpperArm.{side}", "Chest", (sign * 0.18, 0, 1.42), (sign * 0.45, 0, 1.42), 0.05),
        (f"LowerArm.{side}", f"UpperArm.{side}", (sign * 0.45, 0, 1.42), (sign * 0.70, 0, 1.42), 0.045),
        (f"Hand.{side}", f"LowerArm.{side}", (sign * 0.70, 0, 1.42), (sign * 0.82, 0, 1.42), 0.04),
        (f"UpperLeg.{side}", "Hips", (sign * 0.09, 0, 0.95), (sign * 0.09, 0, 0.52), 0.08),
        (f"LowerLeg.{side}", f"UpperLeg.{side}", (sign * 0.09, 0, 0.52), (sign * 0.09, 0, 0.10), 0.06),
        (f"Foot.{side}", f"LowerLeg.{side}", (sign * 0.09, 0, 0.10), (sign * 0.09, -0.16, 0.04), 0.05),
    ]

# ---- armature ---------------------------------------------------------------------------------------------
armature_data = bpy.data.armatures.new("LuxBipedArmature")
armature = bpy.data.objects.new("LuxBiped", armature_data)
scene.collection.objects.link(armature)
bpy.context.view_layer.objects.active = armature
bpy.ops.object.mode_set(mode="EDIT")
edit_bones = {}
for name, parent, head, tail, _radius in BONES:
    bone = armature_data.edit_bones.new(name)
    bone.head, bone.tail = Vector(head), Vector(tail)
    bone.parent = edit_bones.get(parent)
    bone.use_connect = False
    edit_bones[name] = bone
bpy.ops.object.mode_set(mode="OBJECT")

# ---- mesh: a tapered tube per bone ------------------------------------------------------------------------
RING_SEGMENTS = 10
mesh_data = bpy.data.meshes.new("LuxBipedMesh")
builder = bmesh.new()


def add_tube(head, tail, radius_head, radius_tail):
    head, tail = Vector(head), Vector(tail)
    axis = (tail - head).normalized()
    helper = Vector((1, 0, 0)) if abs(axis.x) < 0.9 else Vector((0, 1, 0))
    u = axis.cross(helper).normalized()
    v = axis.cross(u).normalized()
    length = (tail - head).length
    rings = max(4, int(length / 0.04))
    grid = []
    for r in range(rings + 1):
        t = r / rings
        centre = head.lerp(tail, t)
        radius = radius_head + (radius_tail - radius_head) * t
        grid.append([builder.verts.new(centre + radius * (math.cos(a) * u + math.sin(a) * v))
                     for a in (2 * math.pi * s / RING_SEGMENTS for s in range(RING_SEGMENTS))])
    for r in range(rings):
        for s in range(RING_SEGMENTS):
            n = (s + 1) % RING_SEGMENTS
            builder.faces.new((grid[r][s], grid[r][n], grid[r + 1][n], grid[r + 1][s]))
    for end, flip in ((grid[0], True), (grid[-1], False)):
        cap = builder.verts.new(head if flip else tail)
        for s in range(RING_SEGMENTS):
            n = (s + 1) % RING_SEGMENTS
            builder.faces.new((cap, end[n], end[s]) if flip else (cap, end[s], end[n]))


for name, parent, head, tail, radius in BONES:
    add_tube(head, tail, radius, radius * 0.85)

bmesh.ops.recalc_face_normals(builder, faces=builder.faces)
builder.to_mesh(mesh_data)
builder.free()
for polygon in mesh_data.polygons:
    polygon.use_smooth = True

mesh = bpy.data.objects.new("LuxBipedMesh", mesh_data)
scene.collection.objects.link(mesh)
mesh.parent = armature

material = bpy.data.materials.new("LuxBipedMaterial")
material.use_nodes = True
material.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.9, 0.45, 0.2, 1.0)
material.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.55
mesh_data.materials.append(material)

# ---- skin weights: the four nearest bone segments, weighted by inverse squared distance ---------------------
groups = {name: mesh.vertex_groups.new(name=name) for name, *_ in BONES}


def segment_distance(point, head, tail):
    head, tail = Vector(head), Vector(tail)
    direction = tail - head
    t = max(0.0, min(1.0, (point - head).dot(direction) / direction.length_squared))
    return (point - (head + direction * t)).length


for vertex in mesh_data.vertices:
    distances = sorted(((segment_distance(vertex.co, head, tail), name) for name, _p, head, tail, _r in BONES))[:4]
    raw = [(name, 1.0 / (d * d + 1e-4) ** 1.5) for d, name in distances]
    total = sum(w for _n, w in raw)
    for name, w in raw:
        if w / total > 0.02:
            groups[name].add([vertex.index], w / total, "REPLACE")

modifier = mesh.modifiers.new("Armature", "ARMATURE")
modifier.object = armature

# ---- animation ------------------------------------------------------------------------------------------------
bpy.context.view_layer.objects.active = armature
bpy.ops.object.mode_set(mode="POSE")
pose = armature.pose
for pose_bone in pose.bones:
    pose_bone.rotation_mode = "QUATERNION"


def rest_rotation(pose_bone):
    return pose_bone.bone.matrix_local.to_3x3()


def set_world_rotation(name, world_rotation):
    """Rotates a pose bone by `world_rotation` (a Matrix) about its head, as seen from the rest pose."""
    pose_bone = pose.bones[name]
    rest = rest_rotation(pose_bone)
    pose_bone.rotation_quaternion = (rest.transposed() @ world_rotation @ rest).to_quaternion()


def rotation(axis, degrees):
    return Matrix.Rotation(math.radians(degrees), 3, axis)


def clear_pose():
    for pose_bone in pose.bones:
        pose_bone.rotation_quaternion = Quaternion((1, 0, 0, 0))
        pose_bone.location = Vector((0, 0, 0))


def key_all(frame):
    for pose_bone in pose.bones:
        pose_bone.keyframe_insert("rotation_quaternion", frame=frame)
        pose_bone.keyframe_insert("location", frame=frame)


def make_action(name, frames, pose_at):
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    armature.animation_data_create()
    armature.animation_data.action = action
    for frame in frames:
        clear_pose()
        pose_at(frame)
        key_all(frame)
    armature.animation_data.action = None
    return action


def idle(frame):
    phase = (frame - 1) / 60.0 * 2 * math.pi
    set_world_rotation("Chest", rotation("X", 1.5 * math.sin(phase)))
    set_world_rotation("Head", rotation("X", 2.0 * math.sin(phase + 0.8)))
    for side, sign in (("L", 1), ("R", -1)):
        set_world_rotation(f"UpperArm.{side}", rotation("X", 1.0 * math.sin(phase)) @ rotation("Y", sign * 78))
        set_world_rotation(f"LowerArm.{side}", rotation("X", -6))


def walk(frame):
    phase = (frame - 1) / 30.0 * 2 * math.pi
    swing = math.sin(phase)
    for side, sign in (("L", 1), ("R", -1)):
        leg = swing * sign
        set_world_rotation(f"UpperLeg.{side}", rotation("X", 32 * leg))
        # the knee bends while the leg swings forward, and a little on the way back
        set_world_rotation(f"LowerLeg.{side}", rotation("X", -(8 + 38 * max(0.0, math.sin(phase * sign + math.pi / 2)))))
        set_world_rotation(f"Foot.{side}", rotation("X", 10 * leg))
        set_world_rotation(f"UpperArm.{side}", rotation("X", -26 * leg) @ rotation("Y", sign * 78))
        set_world_rotation(f"LowerArm.{side}", rotation("X", -22 - 10 * leg))
    set_world_rotation("Spine", rotation("Z", 5 * swing))
    set_world_rotation("Chest", rotation("Z", -6 * swing))
    set_world_rotation("Head", rotation("Z", 3 * swing))
    bob = 0.035 * abs(math.sin(phase))
    hips = pose.bones["Hips"]
    hips.location = rest_rotation(hips).transposed() @ Vector((0, 0, -bob))
    set_world_rotation("Hips", rotation("Z", -4 * swing))


make_action("Idle", range(1, 62, 4), idle)
make_action("Walk", range(1, 32, 3), walk)
# Leave the rig in its rest pose: FBX stores the node transforms as they stand at export, and an importer
# reads them as the rest pose, which must be the bind pose.
clear_pose()
bpy.ops.object.mode_set(mode="OBJECT")

# ---- export ---------------------------------------------------------------------------------------------------
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.export_scene.fbx(
    filepath=os.path.join(OUT, "LuxBiped.fbx"),
    use_selection=True,
    object_types={"ARMATURE", "MESH"},
    add_leaf_bones=False,
    bake_anim=True,
    bake_anim_use_all_actions=True,
    bake_anim_use_nla_strips=False,
    bake_anim_force_startend_keying=True,
    bake_anim_simplify_factor=0.0,
    mesh_smooth_type="FACE",
    path_mode="AUTO",
)
bpy.ops.export_scene.gltf(
    filepath=os.path.join(OUT, "LuxBiped.gltf"),
    export_format="GLTF_SEPARATE",
    use_selection=True,
    export_animations=True,
    export_animation_mode="ACTIONS",
    export_skins=True,
    export_yup=True,
)
print("LuxBiped written to", OUT, "bones", len(BONES), "vertices", len(mesh_data.vertices))
