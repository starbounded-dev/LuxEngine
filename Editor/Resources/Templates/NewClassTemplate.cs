using Lux;

namespace $NAMESPACE_NAME$
{
	public class $CLASS_NAME$ : global::Lux.Entity
	{
		// Public fields appear in the Properties panel for each entity using this script.
		public float MoveSpeed = 2.0f;

		// Called once when this entity starts playing or is spawned.
		void OnCreate()
		{
			// Set up your entity here.
		}

		// Called every frame. deltaTime is the time since the last frame, in seconds.
		void OnUpdate(float deltaTime)
		{
			// Example: uncomment to move along the X axis at MoveSpeed metres per second.
			// Translation += new global::Lux.Vector3(1.0f, 0.0f, 0.0f) * MoveSpeed * deltaTime;
		}
	}
}
