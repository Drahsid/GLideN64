#include <fstream>
#include <assert.h>
#include <cstdlib>
#include <algorithm>
#include <iomanip>

#include <Graphics/CombinerProgram.h>
#include <Graphics/OpenGLContext/opengl_Utils.h>
#include <Types.h>
#include <Log.h>
#include <RSP.h>
#include <PluginAPI.h>
#include <Combiner.h>
#include <osal_files.h>
#include "glsl_Utils.h"
#include "glsl_ShaderStorage.h"
#include "glsl_CombinerProgramImpl.h"
#include "glsl_CombinerProgramUniformFactory.h"

using namespace glsl;

#define SHADER_STORAGE_FOLDER_NAME L"shaders"

static
void getStorageFileName(const opengl::GLInfo & _glinfo, wchar_t * _shadersFileName, const wchar_t * _fileExtension)
{
	wchar_t strCacheFolderPath[PLUGIN_PATH_SIZE];
	api().GetUserCachePath(strCacheFolderPath);
	wchar_t strShaderFolderPath[PLUGIN_PATH_SIZE];
	swprintf(strShaderFolderPath, PLUGIN_PATH_SIZE, L"%ls/%ls", strCacheFolderPath, SHADER_STORAGE_FOLDER_NAME);
	wchar_t * pPath = strShaderFolderPath;
	if (!osal_path_existsW(strShaderFolderPath) || !osal_is_directory(strShaderFolderPath)) {
		if (osal_mkdirp(strShaderFolderPath) != 0)
			pPath = strCacheFolderPath;
	}

	std::wstring strOpenGLType;

	if(_glinfo.isGLESX) {
		strOpenGLType = L"GLES";
	} else {
		strOpenGLType = L"OpenGL";
	}

	swprintf(_shadersFileName, PLUGIN_PATH_SIZE, L"%ls/GLideN64.%08lx.%ls.%ls", pPath, std::hash<std::string>()(RSP.romname), strOpenGLType.c_str(), _fileExtension);
}

static
u32 _getConfigOptionsBitSet()
{
	std::vector<u32> vecOptions;
	graphics::CombinerProgram::getShaderCombinerOptionsSet(vecOptions);
	u32 optionsSet = 0;
	for (u32 i = 0; i < vecOptions.size(); ++i)
		optionsSet |= vecOptions[i] << i;
	return optionsSet;
}

/*
Storage format:
uint32 - format version;
uint32 - bitset of config options, which may change how shader is created.
uint32 - len of renderer string
char * - renderer string
uint32 - len of GL version string
char * - GL version string
uint32 - number of shaders
shaders in binary form
*/
bool ShaderStorage::saveShadersStorage(const graphics::Combiners & _combiners) const
{
	wchar_t shadersFileName[PLUGIN_PATH_SIZE];
	wchar_t keysFileName[PLUGIN_PATH_SIZE];
	getStorageFileName(m_glinfo, shadersFileName, L"shaders");
	getStorageFileName(m_glinfo, keysFileName, L"keys");

#if defined(OS_WINDOWS) && !defined(MINGW)
	std::ofstream shadersOut(shadersFileName, std::ofstream::binary | std::ofstream::trunc);
	std::ofstream keysOut(keysFileName, std::ofstream::trunc);
#else
	char filename_c[PATH_MAX];
	wcstombs(filename_c, shadersFileName, PATH_MAX);
	std::ofstream shadersOut(filename_c, std::ofstream::binary | std::ofstream::trunc);
	wcstombs(filename_c, keysFileName, PATH_MAX);
	std::ofstream keysOut(filename_c, std::ofstream::trunc);
#endif
	if (!shadersOut || !keysOut)
		return false;

	shadersOut.write((char*)&m_formatVersion, sizeof(m_formatVersion));

	const u32 configOptionsBitSet = _getConfigOptionsBitSet();
	shadersOut.write((char*)&configOptionsBitSet, sizeof(configOptionsBitSet));

	const char * strRenderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
	u32 len = strlen(strRenderer);
	shadersOut.write((char*)&len, sizeof(len));
	shadersOut.write(strRenderer, len);

	const char * strGLVersion = reinterpret_cast<const char *>(glGetString(GL_VERSION));
	len = strlen(strGLVersion);
	shadersOut.write((char*)&len, sizeof(len));
	shadersOut.write(strGLVersion, len);

	len = _combiners.size();

#if 0
	fout.write((char*)&len, sizeof(len));
	for (auto cur = _combiners.begin(); cur != _combiners.end(); ++cur)
		fout << *(cur->second);
#else
	u32 totalWritten = 0;
	std::vector<char> allShaderData;
	std::vector<u64> keysData;
	keysData.reserve(len);

	for (auto cur = _combiners.begin(); cur != _combiners.end(); ++cur)
	{
		std::vector<char> data;
		if (cur->second->getBinaryForm(data))
		{
			keysData.push_back(cur->first.getMux());
			allShaderData.insert(allShaderData.end(), data.begin(), data.end());
			++totalWritten;
		}
		else
		{
			LOG(LOG_ERROR, "Error while writing shader with key key=0x%016lX",
				static_cast<long unsigned int>(cur->second->getKey().getMux()));
		}
	}

	shadersOut.write((char*)&totalWritten, sizeof(totalWritten));
	shadersOut.write(allShaderData.data(), allShaderData.size());

	std::sort(keysData.begin(), keysData.end());
	keysOut << "0x" << std::hex << std::setfill('0') << std::setw(8) << m_keysFormatVersion << "\n";
	for (u64 key : keysData)
		keysOut << "0x" << std::hex << std::setfill('0') << std::setw(16) << key << "\n";

#endif

	shadersOut.flush();
	shadersOut.close();
	keysOut.flush();
	keysOut.close();
	return true;
}

static
CombinerProgramImpl * _readCominerProgramFromStream(std::istream & _is,
	CombinerProgramUniformFactory & _uniformFactory,
	opengl::CachedUseProgram * _useProgram)
{
	CombinerKey cmbKey;
	cmbKey.read(_is);

	int inputs;
	_is.read((char*)&inputs, sizeof(inputs));
	CombinerInputs cmbInputs(inputs);

	GLenum binaryFormat;
	GLint  binaryLength;
	_is.read((char*)&binaryFormat, sizeof(binaryFormat));
	_is.read((char*)&binaryLength, sizeof(binaryLength));
	std::vector<char> binary(binaryLength);
	_is.read(binary.data(), binaryLength);

	GLuint program = glCreateProgram();
	const bool isRect = cmbKey.isRectKey();
	glsl::Utils::locateAttributes(program, isRect, cmbInputs.usesTexture());
	glProgramBinary(program, binaryFormat, binary.data(), binaryLength);
	assert(glsl::Utils::checkProgramLinkStatus(program));

	UniformGroups uniforms;
	_uniformFactory.buildUniforms(program, cmbInputs, cmbKey, uniforms);

	return new CombinerProgramImpl(cmbKey, program, _useProgram, cmbInputs, std::move(uniforms));
}

bool ShaderStorage::_loadFromCombinerKeys(graphics::Combiners & _combiners)
{
	wchar_t keysFileName[PLUGIN_PATH_SIZE];
	getStorageFileName(m_glinfo, keysFileName, L"keys");
#if defined(OS_WINDOWS) && !defined(MINGW)
	std::ifstream fin(keysFileName);
#else
	char fileName_c[PATH_MAX];
	wcstombs(fileName_c, keysFileName, PATH_MAX);
	std::ifstream fin(fileName_c);
#endif
	if (!fin)
		return false;

	u32 version;
	fin >> std::hex >> version;
	if (version != m_keysFormatVersion)
		return false;

	u64 key;
	while (!fin.eof()) {
		fin >> std::hex >> key;
		graphics::CombinerProgram * pCombiner = Combiner_Compile(CombinerKey(key, false));
		pCombiner->update(true);
		_combiners[pCombiner->getKey()] = pCombiner;
	}
	fin.close();

	if (opengl::Utils::isGLError())
		return false;

	return saveShadersStorage(_combiners);
}

bool ShaderStorage::loadShadersStorage(graphics::Combiners & _combiners)
{
	wchar_t shadersFileName[PLUGIN_PATH_SIZE];
	getStorageFileName(m_glinfo, shadersFileName, L"shaders");
	const u32 configOptionsBitSet = _getConfigOptionsBitSet();

#if defined(OS_WINDOWS) && !defined(MINGW)
	std::ifstream fin(shadersFileName, std::ofstream::binary);
#else
	char fileName_c[PATH_MAX];
	wcstombs(fileName_c, fileName, PATH_MAX);
	std::ifstream fin(fileName_c, std::ofstream::binary);
#endif

	if (!fin)
		return _loadFromCombinerKeys(_combiners);

	try {
		u32 version;
		fin.read((char*)&version, sizeof(version));
		if (version != m_formatVersion)
			return _loadFromCombinerKeys(_combiners);

		u32 optionsSet;
		fin.read((char*)&optionsSet, sizeof(optionsSet));
		if (optionsSet != configOptionsBitSet)
			return _loadFromCombinerKeys(_combiners);

		const char * strRenderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
		u32 len;
		fin.read((char*)&len, sizeof(len));
		std::vector<char> strBuf(len);
		fin.read(strBuf.data(), len);
		if (strncmp(strRenderer, strBuf.data(), len) != 0)
			return _loadFromCombinerKeys(_combiners);

		const char * strGLVersion = reinterpret_cast<const char *>(glGetString(GL_VERSION));
		fin.read((char*)&len, sizeof(len));
		strBuf.resize(len);
		fin.read(strBuf.data(), len);
		if (strncmp(strGLVersion, strBuf.data(), len) != 0)
			return _loadFromCombinerKeys(_combiners);

		CombinerProgramUniformFactory uniformFactory(m_glinfo);

		fin.read((char*)&len, sizeof(len));
		for (u32 i = 0; i < len; ++i) {
			CombinerProgramImpl * pCombiner = _readCominerProgramFromStream(fin, uniformFactory, m_useProgram);
			pCombiner->update(true);
			_combiners[pCombiner->getKey()] = pCombiner;
		}
	}
	catch (...) {
		LOG(LOG_ERROR, "Stream error while loading shader cache! Buffer is probably not big enough");
	}

//	m_shadersLoaded = m_combiners.size();
	fin.close();
	return !opengl::Utils::isGLError();
}


ShaderStorage::ShaderStorage(const opengl::GLInfo & _glinfo, opengl::CachedUseProgram * _useProgram)
: m_glinfo(_glinfo)
, m_useProgram(_useProgram)
{
}
