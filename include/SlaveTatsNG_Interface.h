#pragma once

#include <cstdint>
#include "RE/Skyrim.h"

#ifdef interface
#  undef interface
#endif

namespace slavetats
{
	typedef bool fail_t;

	namespace interface
	{
		enum MessageType
		{
			None,
			Interface,
		};

		struct Addresses
		{
			enum
			{
				version = 1,
			};

			uint32_t current_version;

			static const Addresses* from_void(void* iface)
			{
				return ((Addresses*)iface)->current_version == version ? (Addresses*)iface : nullptr;
			}

			fail_t (*simple_add_tattoo)(RE::Actor* target, RE::BSFixedString section, RE::BSFixedString name, int color, bool last, bool silent, float alpha);
			fail_t (*simple_remove_tattoo)(RE::Actor* target, RE::BSFixedString section, RE::BSFixedString name, bool last, bool silent);
			fail_t (*synchronize_tattoos)(RE::Actor* target, bool silent);
			void (*log_tattoo)(RE::BSFixedString message, int tattoo);

			bool (*tattoo_matches)(int a_template, int tattoo, bool include_configurable);
			void (*_append)(int dest, int val);
			void (*_extend_matching)(int dest, int src, int a_template, int applied, RE::BSFixedString domain);
			fail_t (*query_available_tattoos)(int a_template, int matches, int applied, RE::BSFixedString domain);
			fail_t (*query_applied_tattoos)(RE::Actor* target, int a_template, int matches, RE::BSFixedString except_area, int except_slot);
			fail_t (*query_applied_tattoos_with_attribute)(RE::Actor* target, RE::BSFixedString attrib, int matches, RE::BSFixedString except_area, int except_slot);
			bool (*has_applied_tattoos_with_attribute)(RE::Actor* target, RE::BSFixedString attrib, RE::BSFixedString except_area, int except_slot);
			fail_t (*remove_tattoos)(RE::Actor* target, int a_template, bool ignore_lock, bool silent);
			fail_t (*remove_tattoo_from_slot)(RE::Actor* target, RE::BSFixedString area, int slot, bool ignore_lock, bool silent);
			int (*get_applied_tattoo_in_slot)(RE::Actor* target, RE::BSFixedString area, int slot);
			fail_t (*get_applied_tattoos_by_area)(RE::Actor* target, int on_body, int on_face, int on_hands, int on_feet);
			fail_t (*external_slots)(RE::Actor* target, RE::BSFixedString area, int matches);
			fail_t (*add_tattoo)(RE::Actor* target, int tattoo, int slot, bool ignore_lock, bool silent);
			int (*add_and_get_tattoo)(RE::Actor* target, int tattoo, int slot, bool ignore_lock, bool silent, bool try_upgrade);
			RE::TESForm* (*get_form)(int tattoo, RE::BSFixedString plugin_field, RE::BSFixedString formid_field, RE::TESForm* a_default);
			void (*_log_jcontainer)(int jc, RE::BSFixedString indent);
			int (*compile_cache)();
		};

		class singleton
		{
		public:
			const Addresses* iface = nullptr;
			static singleton* get()
			{
				static singleton _singleton;
				return std::addressof(_singleton);
			}

			singleton() = default;
			~singleton() = default;
			singleton(const singleton&) = delete;
			singleton(singleton&&) = delete;
			singleton& operator=(const singleton&) = delete;
			singleton& operator=(singleton&&) = delete;
		};
	}
}
