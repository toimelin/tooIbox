/**************************************************
   Zlib Copyright 2015 Johan Melin
 ***************************************************/

#pragma once

#include <functional>

template<typename Tag, typename Implementation, Implementation DefaultValue>
class Handle {
public:
	static Handle invalid() {
		return Handle();
	}

	// Defaults to Handle::invalid()
	Handle() : m_Val( DefaultValue ) { }

	// Explicit constructor:
	explicit Handle( Implementation val ) : m_Val( val ) { }

	// Explicit conversion to get back the Implementation
	explicit operator Implementation() const {
		return m_Val;
	}

	friend bool operator ==( Handle a, Handle b ) {
		return a.m_Val == b.m_Val;
	}

	friend bool operator !=( Handle a, Handle b ) {
		return a.m_Val != b.m_Val;
	}

	friend bool operator <( Handle a, Handle b ) {
		return a.m_Val < b.m_Val;
	}

	friend bool operator >( Handle a, Handle b ) {
		return a.m_Val > b.m_Val;
	}

private:
	Implementation m_Val;
};

template<typename Tag, typename Implementation, Implementation DefaultValue>
struct HandleHasher {
	size_t operator ()( const Handle<Tag, Implementation, DefaultValue>& handle ) const {
		return std::hash<Implementation>()( static_cast<Implementation>( handle ) );
	}
};
