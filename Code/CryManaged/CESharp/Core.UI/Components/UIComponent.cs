// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

using System;
using CryEngine.UI.Components;

namespace CryEngine.UI
{
	/// <summary>
	/// Enhances UIComponent by some UI specific functionality
	/// </summary>
	public class UIComponent : IUpdateReceiver
	{
		private bool _isActive = false;
		private bool _isActiveByHierarchy = true;

		/// <summary>
		/// Called when the component is added to a SceneObject.
		/// </summary>
		public virtual void OnAwake() { }

		/// <summary>
		/// Called every frame.
		/// </summary>
		public virtual void OnUpdate() { }

		/// <summary>
		/// Called when this component is destroyed.
		/// </summary>
		public virtual void OnDestroy() { }

		/// <summary>
		/// Called when the UIElement of this component gets focused.
		/// </summary>
		public virtual void OnEnterFocus() { }

		/// <summary>
		/// Called when the UIElement of this component loses focus.
		/// </summary>
		public virtual void OnLeaveFocus() { }

		/// <summary>
		/// Called when the mouse is pressed down on the UIElement of this component.
		/// </summary>
		/// <param name="x">The x coordinate.</param>
		/// <param name="y">The y coordinate.</param>
		public virtual void OnLeftMouseDown(int x, int y) { }

		/// <summary>
		/// Called when the mouse button is let go on the UIElement of this component.
		/// </summary>
		/// <param name="x">The x coordinate.</param>
		/// <param name="y">The y coordinate.</param>
		/// <param name="inside">If set to <c>true</c> inside.</param>
		public virtual void OnLeftMouseUp(int x, int y, bool inside) { }

		/// <summary>
		/// Called when the mouse enters the rectangle of the UIElement of this component.
		/// </summary>
		/// <param name="x">The x coordinate.</param>
		/// <param name="y">The y coordinate.</param>
		public virtual void OnMouseEnter(int x, int y) { }

		/// <summary>
		/// Called when the mouse leaves the rectangle of the UIElement of this component.
		/// </summary>
		/// <param name="x">The x coordinate.</param>
		/// <param name="y">The y coordinate.</param>
		public virtual void OnMouseLeave(int x, int y) { }

		/// <summary>
		/// Called when the mouse hovers over the UIElement of this component.
		/// </summary>
		/// <param name="x">The x coordinate.</param>
		/// <param name="y">The y coordinate.</param>
		public virtual void OnMouseMove(int x, int y) { }

		/// <summary>
		/// Called when a key is pressed while this UIElement is focused.
		/// </summary>
		/// <param name="e">The input event.</param>
		public virtual void OnKey(InputEvent e) { }

		/// <summary>
		/// Determines whether this object is Focused (e.g. through processing by Canvas).
		/// </summary>
		/// <value><c>true</c> if has focus; otherwise, <c>false</c>.</value>
		public bool HasFocus { get; private set; }

		/// <summary>
		/// Determines whether object is individually focusable.
		/// </summary>
		/// <value><c>true</c> if enabled; otherwise, <c>false</c>.</value>
		public bool Enabled { get; set; } = true;

		/// <summary>
		/// Owning SceneObject.
		/// </summary>
		/// <value>The owner.</value>
		public SceneObject Owner { get; private set; }

		/// <summary>
		/// Determines whether object is generally focusable.
		/// </summary>
		/// <value><c>true</c> if is focusable; otherwise, <c>false</c>.</value>
		public bool IsFocusable { get; private set; } = false;

		/// <summary>
		/// Root object for this scene tree.
		/// </summary>
		/// <value>The root.</value>
		public SceneObject Root { get { return Owner.Root; } }

		/// <summary>
		/// Transform of this Components owner.
		/// </summary>
		/// <value>The transform.</value>
		public Transform Transform { get { return Owner.Transform; } }

		/// <summary>
		/// Indicates whether this object can be updated.
		/// </summary>
		/// <value><c>true</c> if is updateable; otherwise, <c>false</c>.</value>
		public bool IsUpdateable { get; private set; } = false;

		/// <summary>
		/// Determines whether this object should be updated.
		/// </summary>
		/// <value><c>true</c> if active; otherwise, <c>false</c>.</value>
		public bool Active
		{
			set
			{
				_isActive = value;
			}
			get
			{
				return _isActive;
			}
		}

		/// <summary>
		/// Determines whether this object should be updated on a basis of its ancestors activity.
		/// </summary>
		/// <value><c>true</c> if active by hierarchy; otherwise, <c>false</c>.</value>
		public bool ActiveByHierarchy
		{
			set
			{
				_isActiveByHierarchy = value;
			}
			get
			{
				return _isActiveByHierarchy && Active;
			}
		}

		/// <summary>
		/// Creates an instance of T and wires it into the scene hierarchy.
		/// </summary>
		/// <param name="owner">SceneObject to own this UIComponent.</param>
		/// <typeparam name="T">Type of UIComponent to be instantiated.</typeparam>
		public static T Instantiate<T>(SceneObject owner) where T : UIComponent
		{
			return (T)Instantiate(owner, typeof(T));
		}

		/// <summary>
		/// Registers this component at SceneManager.
		/// </summary>
		/// <param name="order">Intended update order for this component.</param>
		public void TryRegisterUpdateReceiver(int order)
		{
			if(IsUpdateable)
			{
				SceneManager.RegisterUpdateReceiver(this, order);
			}
		}

		void InspectOverrides(Type t)
		{
			var thisType = typeof(UIComponent);
			IsFocusable = t.GetMethod("OnEnterFocus").DeclaringType != thisType || t.GetMethod("OnLeaveFocus").DeclaringType != thisType || t.GetMethod("OnLeftMouseDown").DeclaringType != thisType
				 || t.GetMethod("OnLeftMouseUp").DeclaringType != thisType || t.GetMethod("OnMouseEnter").DeclaringType != thisType || t.GetMethod("OnMouseLeave").DeclaringType != thisType;
			IsUpdateable = t.GetMethod("OnUpdate").DeclaringType != thisType;
		}

		/// <summary>
		/// Called by framework. Do not call directly.
		/// </summary>
		public virtual void Update()
		{
			if(ActiveByHierarchy)
				OnUpdate();
		}

		/// <summary>
		/// Creates an instance of T and wires it into the scene hierarchy.
		/// </summary>
		/// <param name="owner">SceneObject to own this UIComponent.</param>
		/// <param name="t">Type of UIComponent to be instantiated.</param>
		public static UIComponent Instantiate(SceneObject owner, Type t)
		{
			var instance = Activator.CreateInstance(t) as UIComponent;
			instance.Owner = owner;
			instance.InspectOverrides(t);
			instance.OnAwake();
			instance.Active = true;
			return instance;
		}

		/// <summary>
		/// Removes this UIComponent from its parent and unregisters it as UpdateReceiver. Invokes function OnDestroy.
		/// </summary>
		public void Destroy()
		{
			Owner.Components.Remove(this);
			if(IsUpdateable)
			{
				SceneManager.RemoveUpdateReceiver(this);
			}

			OnDestroy();
		}

		/// <summary>
		/// Serializes this UIComponent to JSON string.
		/// </summary>
		/// <returns>The JSON description.</returns>
		public string ToJSON()
		{
			return Tools.ToJSON(this);
		}

		/// <summary>
		/// Invokes the OnEnterFocus message on this component.
		/// </summary>
		public void InvokeOnEnterFocus()
		{
			HasFocus = true;
			OnEnterFocus();
		}

		/// <summary>
		/// Invokes the OnLeaveFocus message on this component.
		/// </summary>
		public void InvokeOnLeaveFocus()
		{
			HasFocus = false;
			OnLeaveFocus();
		}

		/// <summary>
		/// Invokes the OnLeftMouseDown message on this component.
		/// </summary>
		/// <param name="x">The x coordinate.</param>
		/// <param name="y">The y coordinate.</param>
		public void InvokeOnLeftMouseDown(int x, int y)
		{
			OnLeftMouseDown(x, y);
		}

		/// <summary>
		/// Invokes the OnLeftMouseUp message on this component.
		/// </summary>
		/// <param name="x">The x coordinate.</param>
		/// <param name="y">The y coordinate.</param>
		/// <param name="wasInside">If set to <c>true</c> was inside.</param>
		public void InvokeOnLeftMouseUp(int x, int y, bool wasInside)
		{
			OnLeftMouseUp(x, y, wasInside);
		}

		/// <summary>
		/// Invokes the OnMouseEnter message on this component.
		/// </summary>
		/// <param name="x">The x coordinate.</param>
		/// <param name="y">The y coordinate.</param>
		public void InvokeOnMouseEnter(int x, int y)
		{
			OnMouseEnter(x, y);
		}

		/// <summary>
		/// Invokes the OnMouseLeave message on this component.
		/// </summary>
		/// <param name="x">The x coordinate.</param>
		/// <param name="y">The y coordinate.</param>
		public void InvokeOnMouseLeave(int x, int y)
		{
			OnMouseLeave(x, y);
		}

		/// <summary>
		/// Invokes the OnMouseMove message on this component.
		/// </summary>
		/// <param name="x">The x coordinate.</param>
		/// <param name="y">The y coordinate.</param>
		public void InvokeOnMouseMove(int x, int y)
		{
			OnMouseMove(x, y);
		}

		/// <summary>
		/// Invokes the OnKey message on this component.
		/// </summary>
		/// <param name="e">E.</param>
		public void InvokeOnKey(InputEvent e)
		{
			OnKey(e);
		}

		Canvas _parentCanvas;

		/// <summary>
		/// Returns Canvas owning this Conponent in hierarchy.
		/// </summary>
		/// <value>The parent canvas.</value>
		public Canvas ParentCanvas
		{
			get
			{
				if(_parentCanvas == null)
				{
					_parentCanvas = Owner.GetParentWithType<Canvas>();
				}
				return _parentCanvas;
			}
		}

		/// <summary>
		/// Returns Layouted Bounds for this UIComponent.
		/// </summary>
		public virtual Rect GetAlignedRect()
		{
			var rect = Owner.GetComponent<RectTransform>();
			if(rect != null)
			{
				return rect.Bounds;
			}

			return new Rect();
		}

		/// <summary>
		/// Tries to hit Bounds or ClampRect if existing.
		/// </summary>
		/// <returns><c>True</c> if any of the rects was hit.</returns>
		/// <param name="x">The x coordinate.</param>
		/// <param name="y">The y coordinate.</param>
		public virtual bool HitTest(int x, int y)
		{
			var prt = Owner.GetComponent<RectTransform>();

			if(prt == null)
			{
				return false;
			}

			return prt.ClampRect.Size > 0 ? prt.Bounds.Contains(x, y) : prt.ClampRect.Contains(x, y);
		}
	}
}
