#pragma once

#include "log.h"

class Tagged : public std::enable_shared_from_this<Tagged>
{
public:
	static constexpr const char * ClassName = "Tagged";

	Tagged(const str& tag, const str& type = ClassName) : m_tag(tag), m_type(type) { INFO("Creating %s %s", m_type, m_tag); }
	virtual ~Tagged() { INFO("%s %s destroyed", m_type, m_tag); }

	str Tag() const { return m_tag; }
	str Type() const { return m_type; }

private:
	str m_tag, m_type;
};

template <class Owner>
class Collection// : public Mutex<Collection>
{
public:
	Collection() {}
	~Collection() {}

	str UniqueTag(const str& base)
	{
		str tag = base;
		for (int i = 0; m_map.count(tag) || tag == ""; tag = base + std::to_string(i++)) {}
		return tag;
	}

	template <class U = Tagged>
	bool Contains(const str& tag)
	{
		static_assert(std::is_base_of<Tagged, U>::value, "Provided class is not derived from Tagged");
		auto it = m_map.find(tag);
		if (it == m_map.end()) return false;
		return std::dynamic_pointer_cast<U>(it->second) != nullptr;
	}

	template <class U = Tagged>
	std::shared_ptr<U> Find(const str& tag)
	{
		static_assert(std::is_base_of<Tagged, U>::value, "Provided class is not derived from Tagged");
		auto it = m_map.find(tag);
		if (it == m_map.end()) ERROR("Cannot find %s %s", U::ClassName, tag);
		auto p = std::dynamic_pointer_cast<U>(it->second);
		if (!p) ERROR("%s is not %s", tag, U::ClassName);
		return p;
	}

	template <class U = Tagged>
	std::vector<std::shared_ptr<U> > FindAll()
	{
		static_assert(std::is_base_of<Tagged, U>::value, "Provided class is not derived from Tagged");

		std::vector<std::shared_ptr<U> > res;
		for (auto obj : m_map)
			if (auto p = std::dynamic_pointer_cast<U>(obj.second)) res.push_back(p);
		return res;
	}

	template <class U, typename... Args>
	std::shared_ptr<U> New(const str& tag, Args... args)
	{
		static_assert(std::is_base_of<Tagged, U>::value, "Provided class is not derived from Tagged");
		str realTag = tag == "" ? UniqueTag(tag) : tag;
		if (m_map.count(tag)) WARNING("%s %s already exists. Creating %s %s instead", U::ClassName, tag, U::ClassName, realTag = UniqueTag(tag));
		return std::dynamic_pointer_cast<U>(m_map[realTag] = std::shared_ptr<U>(new U(realTag, args...)));
	}

	template <class U, typename... Args>
	std::shared_ptr<U> NewIf(const str& tag, Args... args)
	{
		if (Contains<U>(tag)) return Find<U>(tag);
		else return New<U>(tag, args...);
	}

	void Free(const str& tag)
	{
		auto it = m_map.find(tag);
		if (it == m_map.end()) WARNING("Object %s does not exist", tag);
		else m_map.erase(it);
	}

	void FreeAll()
	{
		m_map.clear();
	}

private:
	std::unordered_map<str, std::shared_ptr<Tagged> > m_map; // multiple short keys
};



template<typename T>
constexpr typename std::enable_if<std::is_base_of<Tagged, T>::value, const char *>::type TypeName()
{
	return T::ClassName;
};

template<typename T>
constexpr typename std::enable_if<!std::is_base_of<Tagged, T>::value, const char *>::type TypeName()
{
	return typeid(T).name();
}

#define CUSTOM_TYPE_NAME(type, custom) template<> constexpr const char * TypeName<type>() { return custom; }
#define AUTO_TYPE_NAME(type) CUSTOM_TYPE_NAME(type, #type)

//CUSTOM_TYPE_NAME(Collection, "Just Collection");
AUTO_TYPE_NAME(bool);
AUTO_TYPE_NAME(str);
#ifndef NO_GL
AUTO_TYPE_NAME(ClearBufferMask);
AUTO_TYPE_NAME(GLenum);
#endif

template<typename T>
inline const str TypeTag()
{
	return "@" + str(TypeName<T>());
}



std::map<str, str>& RegisteredEnums();

template <typename T>
class EnumMap
{
public:
	EnumMap(const std::vector<std::pair<T, str> >& init)
	{
		for (auto p : init)
		{
			m_toStr[p.first] = p.second;
			m_toEnum[p.second] = p.first;
			m_choices += "#" + p.second;
		}
		RegisteredEnums()[TypeName<T>()] = m_choices;
	}
	~EnumMap() {}

	str toStr(const T& e) const
	{
		if (!m_toStr.count(e)) ERROR("No string representation for %s[%x] found", typeid(T).name(), static_cast<unsigned int>(e));
		return m_toStr.at(e);
	}
	T toEnum(const str& s) const
	{
		if (!m_toEnum.count(s)) ERROR("Unkown string representation %s for %s", s, typeid(T).name());
		return m_toEnum.at(s);
	}

	inline str Choices() const { return m_choices; }

private:
	std::map<str, T> m_toEnum; // few enum values
	std::map<T, str> m_toStr;
	str m_choices;
};

template <typename T>
class EnumStore
{
public:
	EnumStore() = delete;

	static const EnumMap<T> Map;
};

template <typename T>
inline str enum2str(const T& e)
{
	static_assert(std::is_enum<T>::value, "T is not enum");
	return EnumStore<T>::Map.toStr(e);
}

template <typename T>
inline T str2enum(const str& s)
{
	static_assert(std::is_enum<T>::value, "T is not enum");
	return EnumStore<T>::Map.toEnum(s);
}



template <class T>
class Group
{
public:
	Properties() {}
	~Group() {}

	inline T Get(const str& name)
	{
		if (!m_properties.count(name)) ERROR("Undefined property %s of type %s", name, typeid(T).name());
		return m_properties.at(name);
	}

	inline std::map<str, T > GetAll()
	{
		return m_properties;
	}

	inline Group& Set(const str& name, const T& value)
	{
		T newValue = value;
		if (Setter(name, newValue))
			m_properties[name] = newValue;
		return *this;
	}

protected:
	virtual bool Setter(const str& name, T& value) = 0;

private:
	template<typename U>
	Group& Set(const str& name, const U& value);

	std::map<str, T > m_properties; // few long keys
};


template <class Owner, class... PropertiesTypes>
class Accessors : public PropertiesTypes...
{
public:
	Accessors(Owner& owner) : m_owner(owner) {}
	~Accessors() {}

// 	template<typename U>
// 	inline U Get(const str& name)
// 	{
// 		return Properties<U>::Get(name);
// 	}

	template<typename U>
	inline typename std::enable_if<!std::is_enum<U>::value && !std::is_base_of<Tagged, U>::value, U>::type Get(const str& name)
	{
		return Group<U>::Get(name);
	}

	template<typename U>
	inline typename std::enable_if<std::is_enum<U>::value, U>::type Get(const str& name)
	{
		return str2enum<U>(Group<str>::Get(name));
	}

// 	template<typename U>
// 	inline Owner& Set(const str& name, const U& value)
// 	{
// 		Properties<U>::Set(name, value);
// 		return m_owner;
// 	}

	template<typename U>
	inline typename std::enable_if<!std::is_enum<U>::value, Owner&>::type Set(const str& name, const U& value)
	{
		Group<U>::Set(name, value);
		return m_owner;
	}

	template<typename U>
	inline typename std::enable_if<std::is_enum<U>::value, Owner&>::type Set(const str& name, const U& value)
	{
		Group<str>::Set(name, enum2str(value));
		return m_owner;
	}

	inline Owner& Set(const str& name, const char * value)
	{
		return Set(name, str(value));
	}

	inline Owner& Set(const str& name, char * value)
	{
		return Set(name, str(value));
	}


	template<typename U>
	inline Owner& MultiSet(const str& name, const U& value)
	{
		return Set(name, value);
	}

	template<typename V1, typename V2, typename... Args>
	inline Owner& MultiSet(const str& name1, const V1& value1, const str& name2, const V2& value2, Args... args)
	{
		MultiSet(name1, value1);
		return MultiSet(name2, value2, args...);
	}

private:
	Owner& m_owner;
};
