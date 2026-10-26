# Prefabs

A **prefab** is a saved entity hierarchy you can place many times. Change the prefab and every copy in your scenes follows.

## Making and placing prefabs

- Select an entity (its children come along) and choose **Edit → Create Prefab from Selection**. The prefab is saved in the Content Browser.
- Drag a prefab from the Content Browser into the Scene Hierarchy to add an instance.
- Right-click a prefab in the Content Browser and choose **Create Variant** for a prefab that starts from another one and keeps following its changes.

## Editing a prefab

Double-click a prefab in the Content Browser to open it on its own. A banner shows that you are editing the prefab. Saving it updates every instance, except the values you changed on an instance yourself.

Changes to the prefab's hierarchy reach every instance too, including instances in scenes that were closed (they catch up when you open the scene):

- A child added to the prefab appears in every instance; a child removed from the prefab leaves them. Anything you added under it in an instance moves up and stays.
- A child the prefab moves to another parent moves in every instance.
- What you did to an instance wins: a prefab child you deleted from an instance stays deleted, one you moved stays where you put it, and children you added are kept.

## Overrides

You can change an instance's components freely. Changed components are marked in the **Prefab** section of the Properties panel:

- **Revert** puts a component back to the prefab's values; **Revert All** does it for the whole entity.
- **Apply** writes your change back into the prefab so every instance gets it; **Apply All** applies every change.
