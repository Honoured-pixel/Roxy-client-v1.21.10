#ifdef _WIN32
#include <Windows.h>
#endif

#include "StringCleaner.hpp"
#include "../LightCoreJar/LightCore.jar.hpp"
#include "../miniz/miniz.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace
{
#ifdef _WIN32
	std::vector<void*> pattern_scan(const std::vector<std::string_view>& patterns);
#endif
	void add_embedded_jar_patterns(std::unordered_set<std::string>& patterns);
	void add_pattern(std::unordered_set<std::string>& patterns, std::string value);
	bool should_add_simple_name(std::string_view internal_name, std::string_view simple_name);

	constexpr std::array seed_patterns =
	{
		"light/core/modules",
		"light/core/events",
		"light/core/Patcher",
		"light.core.modules",
		"light.core.events",
		"light.core.Patcher",
		"$light$core",
		"light/core",
		"light/core/modules/combat/AimAssist",
		"light/core/modules/combat/TriggerBot"

	};
}

void StringCleaner::clean_classfiles()
{
	std::unordered_set<std::string> patterns{};
	for (std::string_view pattern : seed_patterns)
		add_pattern(patterns, std::string(pattern));

	add_embedded_jar_patterns(patterns);

#ifdef _WIN32
	std::vector<std::string_view> pattern_views{};
	pattern_views.reserve(patterns.size());
	for (const std::string& pattern : patterns)
		pattern_views.emplace_back(pattern.data(), pattern.size());

	std::sort(pattern_views.begin(), pattern_views.end(),
		[](std::string_view lhs, std::string_view rhs)
		{
			return lhs.size() > rhs.size();
		});

	pattern_scan(pattern_views);
#endif
}

namespace
{
	void add_embedded_jar_patterns(std::unordered_set<std::string>& patterns)
	{
		mz_zip_archive archive{};
		if (!mz_zip_reader_init_mem(&archive, LightCore_jar.data(), LightCore_jar.size(), 0))
			return;

		mz_uint file_number = mz_zip_reader_get_num_files(&archive);
		for (mz_uint i = 0; i < file_number; ++i)
		{
			if (!mz_zip_reader_is_file_supported(&archive, i) || mz_zip_reader_is_file_a_directory(&archive, i))
				continue;

			std::array<char, 512> filename{};
			mz_zip_reader_get_filename(&archive, i, filename.data(), static_cast<mz_uint>(filename.size()));

			std::string internal_name(filename.data());
			if (!internal_name.ends_with(".class"))
				continue;

			internal_name.resize(internal_name.size() - 6);
			if (!internal_name.starts_with("light/"))
				continue;

			add_pattern(patterns, internal_name);

			std::string dotted_name = internal_name;
			std::replace(dotted_name.begin(), dotted_name.end(), '/', '.');
			add_pattern(patterns, std::move(dotted_name));

			size_t last_separator = internal_name.find_last_of('/');
			std::string_view simple_name = last_separator == std::string::npos ?
				std::string_view(internal_name) :
				std::string_view(internal_name).substr(last_separator + 1);
			if (should_add_simple_name(internal_name, simple_name))
				add_pattern(patterns, std::string(simple_name));
		}

		mz_zip_reader_end(&archive);
	}

	void add_pattern(std::unordered_set<std::string>& patterns, std::string value)
	{
		if (!value.empty())
			patterns.emplace(std::move(value));
	}

	bool should_add_simple_name(std::string_view internal_name, std::string_view simple_name)
	{
		if (simple_name.size() < 8 || simple_name.find('$') != std::string_view::npos)
			return false;

		return internal_name.starts_with("light/core/modules/") ||
			internal_name.starts_with("light/core/events/");
	}
}

#ifdef _WIN32

namespace
{
	bool is_writable_private_page(const MEMORY_BASIC_INFORMATION& mem_info)
	{
		if (mem_info.State != MEM_COMMIT || mem_info.Type != MEM_PRIVATE)
			return false;
		if (mem_info.Protect & (PAGE_GUARD | PAGE_NOACCESS))
			return false;

		switch (mem_info.Protect & 0xFF)
		{
		case PAGE_READWRITE:
		case PAGE_WRITECOPY:
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return true;
		default:
			return false;
		}
	}

	std::vector<void*> pattern_scan(const std::vector<std::string_view>& patterns)
	{
		SYSTEM_INFO sys_info{};
		GetSystemInfo(&sys_info);

		std::vector<void*> results{};
		MEMORY_BASIC_INFORMATION mem_info{};
		for (uint8_t* ptr = (uint8_t*)sys_info.lpMinimumApplicationAddress;
			ptr < sys_info.lpMaximumApplicationAddress && VirtualQuery(ptr, &mem_info, sizeof(MEMORY_BASIC_INFORMATION));
			ptr = (uint8_t*)mem_info.BaseAddress + mem_info.RegionSize)
		{
			if (!is_writable_private_page(mem_info))
				continue;

			std::string_view view((char*)mem_info.BaseAddress, mem_info.RegionSize);
			for (const std::string_view& pattern : patterns)
			{
				for (size_t position = view.find(pattern); position != std::string_view::npos; position = view.find(pattern, position + 1))
				{
					void* found = (uint8_t*)mem_info.BaseAddress + position;
					if (found == pattern.data())
						continue;

					std::memset(found, 0, pattern.size());
					results.push_back(found);
				}
			}
		}
		return results;
	}
}
#endif
