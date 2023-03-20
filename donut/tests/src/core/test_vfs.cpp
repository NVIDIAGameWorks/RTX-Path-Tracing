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

#include <donut/core/vfs/VFS.h>
#include <donut/core/vfs/SQLiteFS.h>

#include <donut/tests/utils.h>
#include <filesystem>

using namespace donut;

std::filesystem::path rpath(DONUT_TEST_SOURCE_DIR);

void test_native_filesystem()
{
	vfs::NativeFileSystem fs;

	// folderExists
	{
		CHECK(fs.folderExists(rpath / "CMakeLists.txt") == false);
		CHECK(fs.folderExists(rpath / "src") == true);
		CHECK(fs.folderExists(rpath / "src/core") == true);
		CHECK(fs.folderExists(rpath / "dummy") == false);
	}

	// fileExists
	{
		CHECK(fs.fileExists(rpath / "CMakeLists.txt")==true);
		CHECK(fs.fileExists(rpath / "src/core/test_vfs.cpp") == true);
		CHECK(fs.fileExists(rpath / "dummy") == false);
	}

	// enumerate
	{
		std::vector<std::string> result;
		CHECK(fs.enumerate(rpath / "*", true, result)==true);
		CHECK(result.size() == 2);
		CHECK(result[0] == "include");
		CHECK(result[1] == "src");
	}
	{
		std::vector<std::string> result;
		CHECK(fs.enumerate(rpath / "CMake*", false, result)==true);
		CHECK(result.size() == 1);
		CHECK(result[0] == "CMakeLists.txt");
	}

	// readFile
	{		
		std::shared_ptr<vfs::IBlob> blob = fs.readFile(rpath / "src/core/test_vfs.cpp");
		CHECK(blob.use_count()>0);
		CHECK(blob->size() > 0);

		std::string data = (char const*)blob->data();
		CHECK(data.find("***HELLO WORLD***")!=std::string::npos);
	}
}

void test_relative_filesystem()
{

	std::shared_ptr<vfs::NativeFileSystem> fs = std::make_shared<vfs::NativeFileSystem>();
	vfs::RelativeFileSystem relativeFS(fs, rpath);

	// folderExists
	{
		CHECK(relativeFS.folderExists("CMakeLists.txt") == false);
		CHECK(relativeFS.folderExists("src") == true);
		CHECK(relativeFS.folderExists("src/core") == true);
		CHECK(relativeFS.folderExists("dummy") == false);
	}

	// fileExists
	{
		CHECK(relativeFS.fileExists("CMakeLists.txt") == true);
		CHECK(relativeFS.fileExists("src/core/test_vfs.cpp") == true);
		CHECK(relativeFS.fileExists(rpath / "CMakeLists.txt") == false);
		CHECK(relativeFS.fileExists("dummy") == false);
	}
	// enumerate
	{
		std::vector<std::string> result;
		CHECK(relativeFS.enumerate("*", true, result) == true);
		CHECK(result.size() == 2);
		CHECK(result[0] == "include");
		CHECK(result[1] == "src");
	}
	{
		std::vector<std::string> result;
		CHECK(relativeFS.enumerate("CMake*", false, result) == true);
		CHECK(result.size() == 1);
		CHECK(result[0] == "CMakeLists.txt");
	}
	// readFile
	{
		std::shared_ptr<vfs::IBlob> blob = relativeFS.readFile("src/core/test_vfs.cpp");
		CHECK(blob.use_count() > 0);
		CHECK(blob->size() > 0);

		std::string data = (char const*)blob->data();
		CHECK(data.find("***HELLO WORLD***") != std::string::npos);
	}
}

void test_root_filesystem()
{
	vfs::RootFileSystem rootFS;

	CHECK(rootFS.unmount("/foo") == false);

	rootFS.mount("/tests", rpath);

	// folderExists
	{
		CHECK(rootFS.folderExists("/tests/CMakeLists.txt") == false);
		CHECK(rootFS.folderExists("/tests/src") == true);
		CHECK(rootFS.folderExists("/tests/src/core") == true);
		CHECK(rootFS.folderExists("/tests/dummy") == false);
	}

	// fileExists
	{
		CHECK(rootFS.fileExists("/tests/CMakeLists.txt") == true);
		CHECK(rootFS.fileExists("/tests/src/core/test_vfs.cpp") == true);
		CHECK(rootFS.fileExists("/CMakeLists.txt") == false);
		CHECK(rootFS.fileExists("/tests/dummy") == false);
	}
	// enumerate
	{
		std::vector<std::string> result;
		CHECK(rootFS.enumerate("/tests/*", true, result) == true);
		CHECK(result.size() == 2);
		CHECK(result[0] == "include");
		CHECK(result[1] == "src");
	}
	{
		std::vector<std::string> result;
		CHECK(rootFS.enumerate("/tests/CMake*", false, result) == true);
		CHECK(result.size() == 1);
		CHECK(result[0] == "CMakeLists.txt");
	}
	// readFile
	{
		std::shared_ptr<vfs::IBlob> blob = rootFS.readFile("/tests/src/core/test_vfs.cpp");
		CHECK(blob.use_count() > 0);
		CHECK(blob->size() > 0);

		std::string data = (char const*)blob->data();
		CHECK(data.find("***HELLO WORLD***") != std::string::npos);
	}

	// unmount
	CHECK(rootFS.unmount("/foo") == false);
	CHECK(rootFS.unmount("/tests") == true);
	CHECK(rootFS.unmount("/foo") == false);
}

void test_sqlite_filesystem()
{
#ifdef DONUT_WITH_SQLITE	
	vfs::SQLiteFileSystem sqliteFS(":memory:", vfs::SQLiteFileSystem::Mode::READ_WRITE_ALLOW_CREATE, "");

	CHECK(sqliteFS.isOpen() == true);

    const std::filesystem::path sampleFileName = "/sample/file.txt";
    const std::filesystem::path sampleFileName2 = "/sample/something.bin";
	const std::filesystem::path sampleFileName3 = "/something.mat.json";
    const std::filesystem::path nonexistentFileName = "/does/not/exist.txt";

    const char* sampleData = "**TESTING SQLITE**'); DROP TABLE files; --"; // simulate an SQL injection ;)
    CHECK(strlen(sampleData) > 0);

    CHECK(sqliteFS.writeFile(sampleFileName, sampleData, strlen(sampleData)));

    auto readData = sqliteFS.readFile(sampleFileName);

    CHECK(readData != nullptr);
    CHECK(readData->size() == strlen(sampleData));
    CHECK(memcmp(readData->data(), sampleData, strlen(sampleData)) == 0);

    readData = sqliteFS.readFile(nonexistentFileName);
    CHECK(readData == nullptr);

    CHECK(sqliteFS.fileExists(sampleFileName) == true);
    CHECK(sqliteFS.fileExists(nonexistentFileName) == false);

    const char* sampleData2 = "2a'YwGWu.U7j$&hG3dmj%.#^H_v<4x4>";
    CHECK(sqliteFS.writeFile(sampleFileName2, sampleData2, strlen(sampleData2)));

	CHECK(sqliteFS.writeFile(sampleFileName3, sampleData2, strlen(sampleData2)));

    std::vector<std::string> matches;
    CHECK(sqliteFS.enumerate("/sample/*.txt", false, matches));
    CHECK(matches.size() == 1);
    CHECK(matches[0] == sampleFileName.generic_string());

    matches.clear();
    CHECK(sqliteFS.enumerate("/*.mat.json", false, matches));
    CHECK(matches.size() == 1);
	CHECK(matches[0] == sampleFileName3.generic_string());

	matches.clear();
	CHECK(sqliteFS.enumerate("/*.ma..json", false, matches));
	CHECK(matches.size() == 0);

    matches.clear();
    CHECK(sqliteFS.enumerate("/*/*", false, matches));
    CHECK(matches.size() == 2);
    CHECK(matches[0] == sampleFileName.generic_string());
    CHECK(matches[1] == sampleFileName2.generic_string());

    matches.clear();
    CHECK(sqliteFS.enumerate("/sample/f??e.*", false, matches));
    CHECK(matches.size() == 1);
    CHECK(matches[0] == sampleFileName.generic_string());
#endif
}

int main(int, char** argv)
{
	try
	{
		test_native_filesystem();
		test_relative_filesystem();
		test_root_filesystem();
		test_sqlite_filesystem();
	}
	catch (const std::runtime_error & err)
	{
		fprintf(stderr, "%s", err.what());
		return 1;
	}
	return 0;
}
