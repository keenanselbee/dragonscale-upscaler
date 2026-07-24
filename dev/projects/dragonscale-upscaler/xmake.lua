set_xmakever('3.0.1')
includes('lib/commonlibsse-ng')

set_project('dragonscale-upscaler')
set_version('1.0')
set_license('GPL-3.0')

set_languages('c++23')
set_warnings('allextra')
set_policy('package.requires_lock', true)
set_toolset('msvc', 'ninja')

add_rules('mode.debug', 'mode.releasedbg', 'mode.release')

local fidelityfx_sdk_dir = path.join(os.scriptdir(), 'external/FidelityFX-SDK-DX11/sdk')
local has_fidelityfx_sdk = os.isdir(path.join(fidelityfx_sdk_dir, 'include/FidelityFX/host/backends/dx11'))
local fidelityfx_lib_dir = path.join(fidelityfx_sdk_dir, 'bin/ffx_sdk')
local fidelityfx_lib_names = {
    'ffx_backend_dx11_x64',
    'ffx_fsr2_x64'
}

local function missing_fidelityfx_static_libs(postfix)
    local missing = {}
    for _, lib in ipairs(fidelityfx_lib_names) do
        if not os.isfile(path.join(fidelityfx_lib_dir, lib .. postfix .. '.lib')) then
            table.insert(missing, lib .. postfix .. '.lib')
        end
    end
    return missing
end

option('skyrim_se')
    set_default(true)
    set_showmenu(true)
    set_description('Build for Skyrim Special Edition')
option_end()

option('skyrim_ae')
    set_default(true)
    set_showmenu(true)
    set_description('Build for Skyrim Anniversary Edition')
option_end()

option('skyrim_vr')
    set_default(false)
    set_showmenu(true)
    set_description('Build for Skyrim VR only')
option_end()

option('copy_to_mod')
    set_default(false)
    set_showmenu(true)
    set_description('Copy the built plugin DLL and PDB into the packaged mod folder after build')
option_end()

if has_config('skyrim_vr') and (has_config('skyrim_se') or has_config('skyrim_ae')) then
    raise('Cannot combine Skyrim VR with SE/AE builds. Enable only one configuration.')
end

target('dragonscale-upscaler')
    add_deps('commonlibsse-ng')

    local runtime = 'se_ae'
    if has_config('skyrim_vr') then
        runtime = 'vr'
    elseif has_config('skyrim_ae') and not has_config('skyrim_se') then
        runtime = 'ae'
    elseif has_config('skyrim_se') and not has_config('skyrim_ae') then
        runtime = 'se'
    end

    add_rules('commonlibsse-ng.plugin', {
        name        = 'dragonscale-upscaler',
        author      = 'Keenan Selbee',
        description = 'An SKSE plugin that implements DLSS and FSR for Skyrim.',
        runtime     = runtime
    })

    add_files('src/**.cpp')
    add_headerfiles('src/**.h')

    add_includedirs(
        'src',
        '$(projectdir)',
        '$(projectdir)/ClibUtil',
        '$(projectdir)/ClibUtil/include',
        '$(projectdir)/xbyak'
    )
    add_syslinks('d3d11', 'dxgi', 'd3dcompiler', 'user32')

    if not has_fidelityfx_sdk then
        raise('FidelityFX DX11 SDK headers are missing. Run tools/fetch-fidelityfx-sdk-dx11.ps1 from the repo root.')
    end

    add_defines('DRAGONSCALE_WITH_FIDELITYFX_SDK')
    add_includedirs(
        path.join(fidelityfx_sdk_dir, 'include'),
        path.join(fidelityfx_sdk_dir, 'src')
    )

    local fidelityfx_lib_postfix = ''
    if is_mode('debug') then
        fidelityfx_lib_postfix = 'd'
    end

    local missing_fidelityfx_libs = missing_fidelityfx_static_libs(fidelityfx_lib_postfix)
    if #missing_fidelityfx_libs > 0 then
        raise('FidelityFX FSR2 static libraries are missing from ' .. fidelityfx_lib_dir .. ': ' .. table.concat(missing_fidelityfx_libs, ', ') .. '. Run tools/build-fidelityfx-sdk-dx11.ps1 from the repo root.')
    end

    add_defines('DRAGONSCALE_WITH_FIDELITYFX_STATIC_LIBS')
    add_linkdirs(fidelityfx_lib_dir)
    for _, lib in ipairs(fidelityfx_lib_names) do
        add_links(lib .. fidelityfx_lib_postfix)
    end
    add_syslinks('dxguid')

    set_pcxxheader('src/pch.h')

    after_build(function (target)
        if has_config('copy_to_mod') then
            local mod_plugin_dir = path.join(os.scriptdir(), '../../../mod/SKSE/Plugins')
            local target_file = target:targetfile()
            local pdb_file = path.join(path.directory(target_file), path.basename(target_file) .. '.pdb')
            os.mkdir(mod_plugin_dir)
            os.cp(target_file, path.join(mod_plugin_dir, path.filename(target_file)))
            if not os.isfile(pdb_file) then
                raise('Expected plugin PDB was not produced: ' .. pdb_file)
            end
            os.cp(pdb_file, path.join(mod_plugin_dir, path.filename(pdb_file)))
        end
    end)

    if has_config('skyrim_vr') then
        add_defines('ENABLE_SKYRIM_VR')
    elseif has_config('skyrim_se') and not has_config('skyrim_ae') then
        add_defines('ENABLE_SKYRIM_SE')
    elseif has_config('skyrim_ae') and not has_config('skyrim_se') then
        add_defines('ENABLE_SKYRIM_AE')
    else
        add_defines('ENABLE_SKYRIM_SE')
        add_defines('ENABLE_SKYRIM_AE')
    end
