/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <donut/core/vfs/ZipFile.h>
#include <donut/core/log.h>
#include <donut/core/string_utils.h>
#include <miniz.h> // declares mz_alloc_func etc. used in miniz_zip.h
#include <miniz_zip.h>
#include <regex>

using namespace donut::vfs;

ZipFile::ZipFile(const std::filesystem::path& archivePath)
{
    m_ArchivePath = archivePath.lexically_normal().generic_string();

    m_ZipArchive = malloc(sizeof(mz_zip_archive));
    memset(m_ZipArchive, 0, sizeof(mz_zip_archive));

    if (!mz_zip_reader_init_file((mz_zip_archive*)m_ZipArchive, m_ArchivePath.c_str(), 
        MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY | MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY))
    {
        const char* errorString = mz_zip_get_error_string(mz_zip_get_last_error((mz_zip_archive*)m_ZipArchive));
        log::warning("Cannot open zip archive '%s': ", m_ArchivePath.c_str(), errorString);

        close();
    }

    mz_uint numFiles = mz_zip_reader_get_num_files((mz_zip_archive*)m_ZipArchive);
    for (mz_uint i = 0; i < numFiles; i++)
    {
        mz_uint nameLength = mz_zip_reader_get_filename((mz_zip_archive*)m_ZipArchive, i, nullptr, 0);
        std::string name;
        name.resize(nameLength - 1); // exclude the trailing zero
        mz_zip_reader_get_filename((mz_zip_archive*)m_ZipArchive, i, name.data(), nameLength);

        if (string_utils::ends_with(name, "/"))
            name.erase(name.size() - 1);

        if (mz_zip_reader_is_file_a_directory((mz_zip_archive*)m_ZipArchive, i))
            m_Directories.insert(name);
        else
            m_Files[name] = i;
    }
;}

ZipFile::~ZipFile()
{
    close();
}

void ZipFile::close()
{
    // make sure we're not closing the file while some other thread is reading from it
    std::lock_guard<std::mutex> lockGuard(m_Mutex);

    if (m_ZipArchive)
    {
        mz_zip_reader_end((mz_zip_archive*)m_ZipArchive);
        free(m_ZipArchive);
        m_ZipArchive = nullptr;
    }
}

bool ZipFile::isOpen() const
{
    return m_ZipArchive != nullptr;
}

bool ZipFile::folderExists(const std::filesystem::path& name)
{
    std::string normalizedName = name.lexically_normal().relative_path().generic_string();

    return m_Directories.find(normalizedName) != m_Directories.end();
}

bool ZipFile::fileExists(const std::filesystem::path& name)
{
    if (!isOpen())
        return false;

    std::string normalizedName = name.lexically_normal().relative_path().generic_string();

    return m_Files.find(normalizedName) != m_Files.end();
}

std::shared_ptr<IBlob> ZipFile::readFile(const std::filesystem::path& name)
{
    if (!isOpen())
        return nullptr;

    std::string normalizedName = name.lexically_normal().relative_path().generic_string();
    
    if (normalizedName.empty())
        return nullptr;
    
    auto entry = m_Files.find(normalizedName);

    if (entry == m_Files.end())
        return nullptr;

    uint32_t fileIndex = entry->second;

    // working with the archive from now on, requires synchronous access
    std::lock_guard<std::mutex> lockGuard(m_Mutex);

    // get information about the file, including its uncompressed size
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat((mz_zip_archive*)m_ZipArchive, fileIndex , &stat))
    {
        const char* errorString = mz_zip_get_error_string(mz_zip_get_last_error((mz_zip_archive*)m_ZipArchive));
        log::warning("Cannot stat file '%s' in zip archive '%s': %s",
            normalizedName.c_str(), m_ArchivePath.c_str(), errorString);

        return nullptr;
    }

    if (stat.m_uncomp_size == 0)
        return nullptr;

    // extract the file
    void* uncompressedData = malloc(stat.m_uncomp_size);
    if (!mz_zip_reader_extract_to_mem((mz_zip_archive*)m_ZipArchive, fileIndex, uncompressedData, stat.m_uncomp_size, 0))
    {
        free(uncompressedData);

        const char* errorString = mz_zip_get_error_string(mz_zip_get_last_error((mz_zip_archive*)m_ZipArchive));
        log::warning("Cannot extract file '%s' from zip archive '%s': %s",
            normalizedName.c_str(), m_ArchivePath.c_str(), errorString);

        return nullptr;
    }

    // package the extracted data into a blob and return
    std::shared_ptr<Blob> blob = std::make_shared<Blob>(uncompressedData, stat.m_uncomp_size);

    return std::static_pointer_cast<IBlob>(blob);
}

bool ZipFile::writeFile(const std::filesystem::path&, const void*, size_t)
{
    // zip files are mounted read-only
    return false;
}

int ZipFile::enumerateFiles(const std::filesystem::path& path, const std::vector<std::string>& extensions, enumerate_callback_t callback, bool allowDuplicates)
{
    (void)allowDuplicates;
    std::basic_regex<char> regex(getFileSearchRegex(path.relative_path(), extensions));

    int numEntries = 0;
    for (const auto& [name, record] : m_Files)
    {
        if (std::regex_match(name, regex))
        {
            std::filesystem::path filePath = name;
            callback(filePath.filename().generic_string());
            ++numEntries;
        }
    }

    return numEntries;
}

int ZipFile::enumerateDirectories(const std::filesystem::path& path, enumerate_callback_t callback, bool allowDuplicates)
{
    (void)allowDuplicates;
    std::filesystem::path normalizedPath = path.relative_path().lexically_normal();

    int numEntries = 0;
    for (const auto& name : m_Directories)
    {
        std::filesystem::path dirPath = name;
        if (dirPath.parent_path() == normalizedPath)
            callback(dirPath.filename().generic_string());
        ++numEntries;
    }
    
    return numEntries;
}
