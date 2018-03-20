using System;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace CryEngine.NativeInternals
{
    internal static class Entity
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        extern public static void RegisterComponent(Type type, ulong guidHipart, ulong guidLopart, string name, string category, string description, string icon);

        [MethodImpl(MethodImplOptions.InternalCall)]
        extern public static void AddComponentBase(Type type, Type baseType);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        extern public static void RegisterEntityWithDefaultComponent(string name, string category, string helper, string icon, bool hide, Type type);

        [MethodImpl(MethodImplOptions.InternalCall)]
        extern public static object GetComponent(IntPtr entityPtr, ulong guidHipart, ulong guidLopart);

		[MethodImpl(MethodImplOptions.InternalCall)]
		extern public static void GetComponents(IntPtr entityPtr, ulong guidHipart, ulong guidLopart, out EntityComponent[] componentsPtr);

        [MethodImpl(MethodImplOptions.InternalCall)]
        extern public static object GetOrCreateComponent(IntPtr entityPtr, ulong guidHipart, ulong guidLopart);

		[MethodImpl(MethodImplOptions.InternalCall)]
        extern public static object AddComponent(IntPtr entityPtr, ulong guidHipart, ulong guidLopart);

        [MethodImpl(MethodImplOptions.InternalCall)]
        extern public static void RegisterComponentProperty(Type type, PropertyInfo propertyInfo, string name, string label, string description, EntityPropertyType propertyType, object defaultValue);

        [MethodImpl(MethodImplOptions.InternalCall)]
        extern public static int GetDetailedRayHitInfo(IntPtr pPhysicalEntityCollider, Vector3 origin, Vector3 direction, float maxRayDist, ref float uOut, ref float vOut);
    }
}