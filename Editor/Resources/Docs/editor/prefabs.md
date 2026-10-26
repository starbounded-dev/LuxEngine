# Prefabs

A **prefab** is a saved entity hierarchy you can place many times. Change the prefab and every copy in your scenes follows.

## Making and placing prefabs

- Select an entity (its children come along) and choose **Edit → Create Prefab from Selection**. The prefab is saved in the Content Browser.
- Drag a prefab from the Content Browser into the Scene Hierarchy to add an instance.
- Right-click a prefab in the Content Browser and choose **Create Variant** for a prefab that starts from another one and keeps following its changes.

## Editing a prefab

Double-click a prefab in the Content Browser to open it on its own. A banner shows that you are editing the prefab. Saving it updates every instance, except the values you changed on an instance yourself.

## Overrides

You can change an instance's components freely. Changed components are marked in the **Prefab** section of the Properties panel:

- **Revert** puts a component back to the prefab's values; **Revert All** does it for the whole entity.
- **Apply** writes your change back into the prefab so every instance gets it; **Apply All** applies every change.
