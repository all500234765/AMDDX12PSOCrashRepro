static const char *shader = R"hlsl(
#define INT_MIN       (-2147483647 - 1)
#define INT_MAX         2147483647

int get_hash_fast( int a)
{
	a = a ^ (a>>4);
	a = (a ^ 0xdeadbeef) + (a << 5);
	a = a ^ (a >> 11);
	return a;
}

int get_hash_1(int a)
{
	a = (a ^ 61) ^ (a >> 16);
	a = a + (a << 3);
	a = a ^ (a >> 4);
	a = a * 0x27d4eb2d;
	a = a ^ (a >> 15);
	return (a % INT_MAX);
}

int get_hash_2(int a, int b, int lod)
{
	int a_0 = b ^ get_hash_1(a) ^ 0x27d4eb2d;
	int b_0 = a ^ get_hash_1(b) ^ 0xdeadbeef;
	return get_hash_fast(a_0 | b_0) % 64 + lod * 64;
}


cbuffer CBufferParameters
{
	int4 _Coord;
};

RWStructuredBuffer<int> hash_table : register(u0, space1);
RWStructuredBuffer<int2> tiles : register(u1, space1);

int generate_new_hash()
{
	int a = _Coord.x;
	int b = _Coord.y;
	for (uint i = 0; i < 2; i++)
	{
		int id = get_hash_2(a, b, _Coord.z);
		if (hash_table[id] == -1)
			return id;

		a = get_hash_1(a);
		b = get_hash_1(b);
	}

	return -1;
}

[numthreads(1, 1, 1)]
void main()
{
	int id = generate_new_hash();
	if (id < 0)
		return;

	hash_table[id] = _Coord.w;
	tiles[_Coord.w] = _Coord.xy;
}
)hlsl";

#include <d3d12.h>
#include <d3dx12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dxgi.lib")

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 615; }
extern "C" { __declspec(dllexport) extern const char *D3D12SDKPath = u8".\\D3D12\\"; }

int main()
{
	////////////////////////////////////////////////////////////////////////
	// For convinience
	////////////////////////////////////////////////////////////////////////
	UINT dxgi_factory_flags = 0;
	ID3D12Debug *debug_controller;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
	{
		debug_controller->EnableDebugLayer();
		dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
	}

	IDXGIFactory2 *dxgi_factory;
	CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&dxgi_factory));

	////////////////////////////////////////////////////////////////////////
	// Search for AMD card so that driver can try to compile such PSO and crash
	// hence reproducing the issue
	////////////////////////////////////////////////////////////////////////
	enum class Vendor { AMD, NVIDIA };
	const Vendor vendor = Vendor::AMD;

	IDXGIAdapter1 *adapter = nullptr;
	int index = 0;
	while (dxgi_factory->EnumAdapters1(index++, &adapter) == S_OK)
	{
		DXGI_ADAPTER_DESC desc;
		adapter->GetDesc(&desc);

		if (vendor == Vendor::AMD)
		{
			if (wcsstr(desc.Description, L"AMD") != nullptr)
				break;
			if (wcsstr(desc.Description, L"Radeon") != nullptr)
				break;
		} else if (vendor == Vendor::NVIDIA)
		{
			if (wcsstr(desc.Description, L"NVIDIA") != nullptr)
				break;
		}

		adapter = nullptr;
	}

	if (adapter == nullptr)
	{
		const char *vendor_name[2] = { "AMD", "NVIDIA" };
		printf_s("No %s graphics card found\n", vendor_name[int(vendor)]);
		return 0;
	}

	ID3D12Device *device;
	D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));

	IDxcCompiler3 *dxc_compiler;
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxc_compiler));

	std::vector<LPCWSTR> arguments;

	arguments.push_back(L"-E");
	arguments.push_back(L"main");

	arguments.push_back(L"-T");
	arguments.push_back(L"cs_6_6");

	arguments.push_back(L"-HV");
	arguments.push_back(L"2018");

	arguments.push_back(L"-Qstrip_debug");

	////////////////////////////////////////////////////////////////////////
	// Changing to O2 fixes issue with AMD driver not being able to compile
	////////////////////////////////////////////////////////////////////////
	arguments.push_back(DXC_ARG_OPTIMIZATION_LEVEL3);

	arguments.push_back(DXC_ARG_ENABLE_STRICTNESS);
	arguments.push_back(DXC_ARG_PACK_MATRIX_COLUMN_MAJOR);


	DxcBuffer dxc_buffer = {};
	dxc_buffer.Ptr = shader;
	dxc_buffer.Size = strlen(shader);

	IDxcResult *result;
	dxc_compiler->Compile(&dxc_buffer, arguments.data(), arguments.size(), nullptr, IID_PPV_ARGS(&result));

	IDxcBlob *shader_blob;
	IDxcBlob *error_blob;
	result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader_blob), nullptr);
	result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&error_blob), nullptr);
	if (error_blob->GetBufferSize())
		printf_s("Shader Errors:\n%s\n", static_cast<char *>(error_blob->GetBufferPointer()));

	std::vector<D3D12_ROOT_PARAMETER1> root_parameters;
	CD3DX12_ROOT_PARAMETER1 root_parameter{};
	root_parameter.InitAsConstantBufferView(0);
	root_parameters.push_back(root_parameter);

	std::vector<D3D12_DESCRIPTOR_RANGE1> descriptor_ranges;
	CD3DX12_DESCRIPTOR_RANGE1 descriptor_range{};
	descriptor_range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 1);
	descriptor_ranges.push_back(descriptor_range);

	root_parameter.InitAsDescriptorTable(1, descriptor_ranges.data());
	root_parameters.push_back(root_parameter);

	D3D12_ROOT_SIGNATURE_FLAGS root_signature_flags = {}; // Do not seem to matter in this repro

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc{};
	root_signature_desc.Init_1_1(root_parameters.size(), root_parameters.data(), 0, nullptr, root_signature_flags);

	ID3DBlob *signature = {};
	ID3DBlob *error = {};
	D3DX12SerializeVersionedRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);

	ID3D12RootSignature *root_signature;
	device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_signature));

	D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc{};
	pso_desc.CS.pShaderBytecode = shader_blob->GetBufferPointer();
	pso_desc.CS.BytecodeLength = shader_blob->GetBufferSize();
	pso_desc.pRootSignature = root_signature;

	ID3D12PipelineState *pso;
	////////////////////////////////////////////////////////////////////////
	// And we crash here...
	// You can try changing optimization flag from O3 to O2 and it should create it successfully
	// Same with changing cs_6_6 to cs_6_5
	////////////////////////////////////////////////////////////////////////
	device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso));

	return 1;
}
