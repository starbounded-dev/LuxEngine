// Vercidium Audio SDK - occlusion does not respond to scene geometry
//
// SDK:      Vercidium Audio v1.8.0, native C API (3d/native), production build
// Library:  libvaudionative.so (Linux x86_64)
// Platform: Arch Linux, x86_64, clang, glibc
//
// Build:
//   clang occlusion_not_responding_to_geometry.c \
//     -I <sdk>/3d/native/include \
//     -L <sdk>/3d/native/production/linux -lvaudionative -lm \
//     -Wl,-rpath,<sdk>/3d/native/production/linux \
//     -o occlusion_repro
//
// SUMMARY
// -------
// A listener emitter casts every ray type and targets a source emitter across a concrete wall. As
// the wall grows, the listener's *reverb* results respond exactly as expected — returnedPercent
// rises and outsidePercent falls, monotonically, proving the geometry is in the world and is being
// hit by rays from this very emitter. Over the same sweep, the occlusion filter returned by
// vaEmitterGetTargetFilter() is bit-identical in every configuration, including with no geometry in
// the world at all:
//
//     gainLF = 0.998234   gainHF = 0.995276
//
// So this is not a setup problem: the same emitter, in the same update, demonstrably sees the
// geometry for reverb purposes and ignores it for occlusion purposes.
//
// Both emitter topologies were tested and behave identically (see DIRECTION below), as were
// occlusion bounce counts from 1 to 32 and wall thicknesses from 1 m to 4 m.
//
// ONE FURTHER DATA POINT
// ----------------------
// The filter is not permanently frozen: placing both emitters *inside* a solid concrete prism
// yields gainLF = gainHF = 0.0. So the occlusion path appears to distinguish "emitter is inside
// solid geometry" from "emitter is not", while ignoring geometry that merely lies between the
// listener and its target.

#include "vaudio.h"
#include <stdio.h>

typedef enum { DIR_LISTENER_TARGETS_SOURCE, DIR_SOURCE_TARGETS_LISTENER } Direction;

// Ray counts from the getting-started example in the documentation.
static void ConfigureCaster(VAEmitter* e)
{
    vaEmitterSetHasRelativeReverb(e, true);
    vaEmitterSetAffectsGroupedEAX(e, false);
    vaEmitterSetReverbRayCount(e, 128);            vaEmitterSetReverbBounceCount(e, 64);
    vaEmitterSetOcclusionRayCount(e, 512);         vaEmitterSetOcclusionBounceCount(e, 8);
    vaEmitterSetPermeationRayCount(e, 128);        vaEmitterSetPermeationBounceCount(e, 3);
    vaEmitterSetAmbientOcclusionRayCount(e, 512);  vaEmitterSetAmbientOcclusionBounceCount(e, 8);
    vaEmitterSetAmbientPermeationRayCount(e, 128); vaEmitterSetAmbientPermeationBounceCount(e, 4);
}

// wallW <= 0 means "add no geometry at all". The wall sits at the origin, between the emitters.
static void RunCase(const char* label, Direction dir, float wallW, float wallH, float wallThickness)
{
    VAWorld* world = vaWorldCreate();
    vaWorldSetPosition(world, vaVectorCreate(-200, -200, -200));
    vaWorldSetSize(world, vaVectorCreate(400, 400, 400));

    if (wallW > 0.0f)
    {
        VAPrismPrimitive* wall = vaPrismPrimitiveCreate();
        vaPrismPrimitiveSetMaterial(wall, VAMaterialConcrete);
        vaPrismPrimitiveSetSize(wall, vaVectorCreate(wallW, wallH, wallThickness));
        vaWorldAddPrimitive_(world, wall);
    }

    VAEmitter* listener = vaEmitterCreate();
    VAEmitter* source   = vaEmitterCreate();

    VAEmitter* caster = (dir == DIR_LISTENER_TARGETS_SOURCE) ? listener : source;
    VAEmitter* target = (dir == DIR_LISTENER_TARGETS_SOURCE) ? source   : listener;
    ConfigureCaster(caster);

    vaWorldAddEmitter(world, listener);
    vaWorldAddEmitter(world, source);
    vaEmitterSetPosition(listener, vaVectorCreate(0, 0, -5));
    vaEmitterSetPosition(source,   vaVectorCreate(0, 0,  5));
    vaEmitterSetMaxVolume(source, 1.0f);

    vaEmitterAddTarget(caster, target);

    for (int i = 0; i < 200; i++)
    {
        vaWorldUpdate(world);
        vaWorldWait(world);
    }

    printf("  %-26s castsRays=%d raytracedTarget=%d  ", label,
           vaEmitterGetCastsAnyRays(caster), vaEmitterHasRaytracedTarget(caster, target));

    VALowPassFilter* filter = vaEmitterGetTargetFilter(caster, target);
    if (filter)
        printf("OCCLUSION gainLF=%.6f gainHF=%.6f", filter->gainLF, filter->gainHF);
    else
        printf("OCCLUSION filter=NULL             ");

    // Printed alongside on purpose: this is the control. If these move and the occlusion gains
    // above do not, the rays are reaching the geometry and only the occlusion result ignores it.
    VAProcessedReverb* reverb = vaEmitterGetProcessedReverb(caster);
    if (reverb)
        printf("   [control] REVERB returned=%.4f outside=%.4f", reverb->returnedPercent, reverb->outsidePercent);

    printf("\n");

    // Documented teardown order: drain the worker threads before destroying the world.
    vaWorldSetPendingShutdown(world, true);
    for (int i = 0; i < 1000 && vaWorldGetThreadsRunning(world); i++)
    {
        vaWorldUpdate(world);
        vaWorldWait(world);
    }
    vaEmitterRemoveTarget(caster, target);
    if (vaWorldRemoveEmitter(world, source)   == VA_SUCCESS) vaEmitterDestroy(source);
    if (vaWorldRemoveEmitter(world, listener) == VA_SUCCESS) vaEmitterDestroy(listener);
    vaWorldDestroy(world);
}

int main(void)
{
    int major = 0, minor = 0, patch = 0;
    vaGetVersion(&major, &minor, &patch);
    printf("Vercidium Audio %d.%d.%d (production build: %s)\n\n", major, minor, patch,
           vaIsProduction() ? "yes" : "no");
    printf("Listener at z=-5, source at z=+5. Concrete wall at the origin, between them.\n");
    printf("Growing the wall should increase occlusion. The reverb columns are the control.\n\n");

    printf("A) listener.AddTarget(source), ray counts on the listener (the documented arrangement):\n");
    RunCase("no wall at all",          DIR_LISTENER_TARGETS_SOURCE,   0,  0, 0);
    RunCase("wall   1 x 1 m",          DIR_LISTENER_TARGETS_SOURCE,   1,  1, 1);
    RunCase("wall   4 x 3 m",          DIR_LISTENER_TARGETS_SOURCE,   4,  3, 1);
    RunCase("wall  20 x 15 m",         DIR_LISTENER_TARGETS_SOURCE,  20, 15, 1);
    RunCase("wall 100 x 75 m",         DIR_LISTENER_TARGETS_SOURCE, 100, 75, 1);
    RunCase("wall 100 x 75 m, 4m thick", DIR_LISTENER_TARGETS_SOURCE, 100, 75, 4);

    printf("\nB) source.AddTarget(listener), ray counts on the source (the mirror image):\n");
    RunCase("no wall at all",          DIR_SOURCE_TARGETS_LISTENER,   0,  0, 0);
    RunCase("wall   4 x 3 m",          DIR_SOURCE_TARGETS_LISTENER,   4,  3, 1);
    RunCase("wall 100 x 75 m",         DIR_SOURCE_TARGETS_LISTENER, 100, 75, 1);

    return 0;
}
