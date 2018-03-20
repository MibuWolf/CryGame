// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

namespace CryEngine.Animations
{
	public sealed class AnimationTag
	{
		/// <summary>
		/// The name of this AnimationTag.
		/// </summary>
		/// <value>The name.</value>
		public string Name { get; private set; }

		/// <summary>
		/// The ID of this AnimationTag.
		/// </summary>
		/// <value>The identifier.</value>
		public int Id { get; private set; }

		internal AnimationTag(string name, int id)
		{
			Name = name;
			Id = id;
		}
	}
}
