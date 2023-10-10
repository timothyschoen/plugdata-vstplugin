/*
 // Copyright (c) 2021-2022 Timothy Schoen and Pierre Guillot
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#include <juce_gui_basics/juce_gui_basics.h>
#include "Utility/Config.h"

extern "C" {

#include <m_pd.h>

t_glist* clone_get_instance(t_gobj*, int);
int clone_get_n(t_gobj*);
}

#include "AllGuis.h"
#include "Pd/Instance.h"
#include "Pd/Patch.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Canvas.h"
#include "Object.h"
#include "Sidebar/Palettes.h"
#include "ObjectBase.h"

#include "ImplementationBase.h"
#include "ObjectImplementations.h"

ImplementationBase::ImplementationBase(t_gobj* obj, PluginProcessor* processor)
    : pd(processor)
    , ptr(obj, processor)
{
    update();
}

ImplementationBase::~ImplementationBase() = default;

Canvas* ImplementationBase::getMainCanvasForObject(t_gobj* objectPtr) const
{
    for(auto* editor : pd->openedEditors) {
        for (auto* cnv : editor->canvases) {
            for (auto* object : cnv->objects) {
                if(object->getPointer() == objectPtr)
                {
                    return cnv;
                }
            }
        }
    }

    return nullptr;
}

Canvas* ImplementationBase::getMainCanvas(t_canvas* patchPtr) const
{
    for(auto* editor : pd->openedEditors) {
        for (auto* cnv : editor->canvases) {
            auto glist = cnv->patch.getPointer();
            if (glist && glist.get() == patchPtr) {
                return cnv;
            }
        }
    }

    return nullptr;
}

bool ImplementationBase::hasImplementation(char const* type)
{
    switch (hash(type)) {
    case hash("canvas"):
    case hash("graph"):
    case hash("key"):
    case hash("keyname"):
    case hash("keyup"):
    case hash("canvas.active"):
    case hash("canvas.mouse"):
    case hash("canvas.vis"):
    case hash("canvas.zoom"):
    case hash("canvas.edit"):
    case hash("mouse"):
        return true;

    default:
        return false;
    }
}
ImplementationBase* ImplementationBase::createImplementation(String const& type, t_gobj* ptr, PluginProcessor* pd)
{
    switch (hash(type)) {
    case hash("canvas"):
    case hash("graph"):
        return new SubpatchImpl(ptr, pd);
    case hash("canvas.active"):
        return new CanvasActiveObject(ptr, pd);
    case hash("canvas.mouse"):
        return new CanvasMouseObject(ptr, pd);
    case hash("canvas.vis"):
        return new CanvasVisibleObject(ptr, pd);
    case hash("canvas.zoom"):
        return new CanvasZoomObject(ptr, pd);
    case hash("canvas.edit"):
        return new CanvasEditObject(ptr, pd);
    case hash("key"):
        return new KeyObject(ptr, pd, KeyObject::Key);
    case hash("keyname"):
        return new KeyObject(ptr, pd, KeyObject::KeyName);
    case hash("keyup"):
        return new KeyObject(ptr, pd, KeyObject::KeyUp);
    case hash("mouse"):
        return new MouseObject(ptr, pd);

    default:
        break;
    }

    return nullptr;
}

void ImplementationBase::openSubpatch(pd::Patch* subpatch)
{
    if (!subpatch) {
        if (auto glist = ptr.get<t_glist>()) {
            subpatch = new pd::Patch(glist.get(), pd, false);
        }
    }

    File path;
    if (auto glist = ptr.get<t_glist>()) {
        if (canvas_isabstraction(glist.get())) {
            path = File(String::fromUTF8(canvas_getdir(glist.get())->s_name)).getChildFile(String::fromUTF8(glist->gl_name->s_name)).withFileExtension("pd");
        }
    } else {
        return;
    }

    pd->patches.add(subpatch);

    subpatch->setCurrentFile(path);

    for(auto* editor : pd->openedEditors) {
        if(!editor->isActiveWindow()) continue;
        
        // Check if subpatch is already opened
        for (auto* cnv : editor->canvases) {
            if (cnv->patch == *subpatch) {
                auto* tabbar = cnv->getTabbar();
                tabbar->setCurrentTabIndex(cnv->getTabIndex());
                return;
            }
        }

        auto* newCanvas = editor->canvases.add(new Canvas(editor, subpatch, nullptr));
        editor->addTab(newCanvas);
    }
}

void ImplementationBase::closeOpenedSubpatchers()
{
    auto glist = ptr.get<t_glist>();
    if (!glist)
        return;
    
    for(auto* editor : pd->openedEditors) {
        for (auto* canvas : editor->canvases) {
            auto canvasPtr = canvas->patch.getPointer();
            if (canvasPtr && canvasPtr.get() == glist.get()) {
                canvas->editor->closeTab(canvas);
                break;
            }
        }
    }
}

ObjectImplementationManager::ObjectImplementationManager(pd::Instance* processor)
    : pd(static_cast<PluginProcessor*>(processor))
{
}

void ObjectImplementationManager::handleAsyncUpdate()
{
    Array<t_gobj*> allImplementations;

    pd->setThis();

    pd->lockAudioThread();
    t_glist* x;
    for (x = pd_getcanvaslist(); x; x = x->gl_next) {
        allImplementations.addArray(getImplementationsForPatch(x));
    }
    pd->unlockAudioThread();

    // Remove unused object implementations
    for (auto it = objectImplementations.cbegin(); it != objectImplementations.cend();) {
        auto& [ptr, implementation] = *it;
        auto found = std::find(allImplementations.begin(), allImplementations.end(), ptr);

        if (found == allImplementations.end()) {
            objectImplementations.erase(it++);
        } else {
            it++;
        }
    }

    for (auto* ptr : allImplementations) {
        if (!objectImplementations.count(ptr)) {

            auto const name = String::fromUTF8(pd::Interface::getObjectClassName(&ptr->g_pd));

            objectImplementations[ptr] = std::unique_ptr<ImplementationBase>(ImplementationBase::createImplementation(name, ptr, pd));
        }

        objectImplementations[ptr]->update();
    }
}

void ObjectImplementationManager::updateObjectImplementations()
{
    triggerAsyncUpdate();
}

Array<t_gobj*> ObjectImplementationManager::getImplementationsForPatch(t_canvas* patch)
{
    Array<t_gobj*> implementations;

    auto* glist = static_cast<t_glist*>(patch);
    for (t_gobj* y = glist->gl_list; y; y = y->g_next) {

        auto const* name = pd::Interface::getObjectClassName(&y->g_pd);

        if (pd_class(&y->g_pd) == canvas_class) {
            implementations.addArray(getImplementationsForPatch(reinterpret_cast<t_canvas*>(y)));
        }
        if (pd_class(&y->g_pd) == clone_class) {
            for (int i = 0; i < clone_get_n(y); i++) {
                auto* clone = clone_get_instance(y, i);
                implementations.addArray(getImplementationsForPatch(clone));
                implementations.add(&clone->gl_obj.te_g);
            }
        }
        if (ImplementationBase::hasImplementation(name)) {
            implementations.add(y);
        }
    }

    return implementations;
}

void ObjectImplementationManager::clearObjectImplementationsForPatch(t_canvas* patch)
{
    auto* glist = static_cast<t_glist*>(patch);

    for (t_gobj* y = glist->gl_list; y; y = y->g_next) {
        if (pd_class(&y->g_pd) == canvas_class) {
            clearObjectImplementationsForPatch(reinterpret_cast<t_canvas*>(y));
        }
        if (pd_class(&y->g_pd) == clone_class) {
            for (int i = 0; i < clone_get_n(y); i++) {
                auto* clone = clone_get_instance(y, i);
                clearObjectImplementationsForPatch(clone);
            }
        }
        objectImplementations.erase(y);
    }
}
