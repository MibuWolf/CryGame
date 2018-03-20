#pragma once

#include "MonoLibrary.h"
#include "MonoString.h"

#ifndef HAVE_MONO_API
namespace MonoInternals
{
	struct MonoDomain;
}
#endif

// Represents an application domain: https://msdn.microsoft.com/en-us/library/2bh4z9hs(v=vs.110).aspx
// Each application domain can load assemblies in a "Sandbox" fashion.
// Assemblies can only be unloaded by unloading the domain that they reside in, with the exception of assemblies in the root domain that is only unloaded on shutdown.
class CMonoDomain
{
	friend class CAppDomain;
	friend class CMonoLibrary;
	friend class CMonoClass;
	friend class CMonoRuntime;

protected:
	CMonoDomain() = default;
	virtual ~CMonoDomain();

	// Begin public API
public:
	// Whether or not this domain is the root one
	virtual bool IsRoot() { return false; }

	// Call to make this the currently active domain
	bool Activate(bool bForce = false);
	// Used to check if this domain is currently active
	bool IsActive() const;

	// Called to unload an app domain and then reload it afterwards, useful to use newly compiled assemblies without restarting
	virtual bool Reload() = 0;

	std::shared_ptr<CMonoString> CreateString(const char* szString);
	static std::shared_ptr<CMonoString> CreateString(MonoInternals::MonoString* pManagedString);
	
	CMonoLibrary* LoadLibrary(const char* szPath);
	CMonoLibrary* GetLibraryFromMonoAssembly(MonoInternals::MonoAssembly* pAssembly);

	MonoInternals::MonoDomain* GetHandle() const { return m_pDomain; }

protected:
	void Unload();

	MonoInternals::MonoDomain* GetMonoDomain() const { return m_pDomain; }

protected:
	MonoInternals::MonoDomain *m_pDomain;
	// Whether or not this domain was created on the native side
	bool m_bNativeDomain;

	std::vector<std::unique_ptr<CMonoLibrary>> m_loadedLibraries;
};