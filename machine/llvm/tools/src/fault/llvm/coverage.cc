/**
	# Query profile output and binaries for counts and region area information.

	# [ Engineering ]
	# Most of this can be eliminated as llvm-cov added the export command in LLVM-4.0.
	# Currently, this has *not* been merged into Xcode's LLVM as of Catalina, but
	# considering the pain of supporting this extension, removing it would be
	# beneficial.
*/
#include <stddef.h>
#include <limits.h>

extern "C" {
#include <fault/libc.h>
#include <Python.h>
#include <fault/python/environ.h>
#include <fault/internal.h>
}

#ifdef isalnum
	#undef isalnum
#endif
#ifdef isalpha
	#undef isalpha
#endif
#ifdef islower
	#undef islower
#endif
#ifdef isspace
	#undef isspace
#endif
#ifdef isupper
	#undef isupper
#endif
#ifdef tolower
	#undef tolower
#endif
#ifdef toupper
	#undef toupper
#endif

#undef DEBUG

#define __STDC_LIMIT_MACROS 1
#define __STDC_CONSTANT_MACROS 1

#include <ctype.h>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallBitVector.h"

/*
	# CounterMappingRegion (mapping stored in binaries)
*/
#if (LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9)
	#include "llvm/ProfileData/CoverageMapping.h"
	#include "llvm/ProfileData/CoverageMappingReader.h"
#else
	#include "llvm/ProfileData/Coverage/CoverageMapping.h"
	#include "llvm/ProfileData/Coverage/CoverageMappingReader.h"
#endif

#if (LLVM_VERSION_MAJOR >= 5)
	#define CM_LOAD(object, data, arch) coverage::CoverageMapping::load( \
		makeArrayRef(StringRef(object)), \
		StringRef(data), \
		StringRef(arch) \
	)
#else
	#define CM_LOAD coverage::CoverageMapping::load
#endif

#if (LLVM_VERSION_MAJOR >= 9)
	#define POSTv9(...) __VA_ARGS__
	#define CREATE_READER(BUF, ARCH, OBJBUFS) \
		coverage::BinaryCoverageReader::create(BUF->getMemBufferRef(), ARCH, OBJBUFS)
	#define ITER_CR_RECORDS(V, I) \
		for (const auto &_cov : I) \
		{ \
			for (auto V : (*_cov))

	#define ITER_CR_CLOSE() }
#else
	#define POSTv9(...)
	#define CREATE_READER(BUF, ARCH, OBJBUFS) \
		coverage::BinaryCoverageReader::create(BUF, ARCH)
	#define ITER_CR_RECORDS(V, I) \
		for (auto V : (*I))
	#define ITER_CR_CLOSE()
#endif

#include "llvm/ProfileData/InstrProfReader.h"

#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <system_error>
#include <tuple>
#include <iostream>
#include <set>

using namespace llvm;

extern "C" {

static int kind_map[] = {
	1, -1, 0,
};

#if (LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9)
	#define CRE_GET_ERROR(X) X.getError()
	#define CMR_GET_ERROR(X) NULL
	#define ERR_STRING(X) X.message().c_str()
	#define RECORD(X) (X)
#else
	#define CRE_GET_ERROR(X) (X.takeError())
	#define CMR_GET_ERROR(X) (X.takeError())
	#define ERR_STRING(X) toString(std::move(X)).c_str()
	#define RECORD(X) (*X)
#endif

#if (LLVM_VERSION_MAJOR == 4)
	#undef CMR_GET_ERROR
	#define CMR_GET_ERROR(X) NULL
	#undef RECORD
	#define RECORD(X) (X)
#endif

/**
	# CounterMappingRegion is per-function and contains start-stop line/column pairs.
	# CoverageArch is the target triple commonly used by clang/gcc
*/
int
_list_regions(PyObj seq, char *object, char *arch)
{
	auto CounterMappingBuff = MemoryBuffer::getFile(object);

	if (std::error_code EC = CounterMappingBuff.getError())
	{
		PyErr_SetString(PyExc_RuntimeError, EC.message().c_str());
		return(1);
	}

	POSTv9(SmallVector<std::unique_ptr<MemoryBuffer>, 4> bufs);

	auto CoverageReaderOrErr = CREATE_READER(CounterMappingBuff.get(), arch, bufs);
	if (!CoverageReaderOrErr)
	{
		if (auto E = CRE_GET_ERROR(CoverageReaderOrErr))
		{
			PyErr_SetString(PyExc_RuntimeError, ERR_STRING(E));
			return(1);
		}

		PyErr_SetString(PyExc_RuntimeError, "failed to load counter mapping reader from object");
		return(1);
	}

	ITER_CR_RECORDS(R, CoverageReaderOrErr.get())
	{
		if (auto E = CMR_GET_ERROR(R))
			continue;

		const auto &record = RECORD(R);
		auto fname = record.FunctionName;
		PyObj subseq, paths, fname_ob;

		fname_ob = PyUnicode_FromStringAndSize(fname.data(), fname.size());
		subseq = PyList_New(0);
		paths = PyList_New(0);

		PyObj x = PyTuple_New(3);
		PyTuple_SET_ITEM(x, 0, paths);
		PyTuple_SET_ITEM(x, 1, fname_ob);
		PyTuple_SET_ITEM(x, 2, subseq);
		PyList_Append(seq, x);
		Py_DECREF(x);
		x = NULL;

		for (auto path : record.Filenames)
		{
			PyObj path_ob = PyUnicode_FromStringAndSize(path.data(), path.size());
			PyList_Append(paths, path_ob);
			Py_DECREF(path_ob);
		}

		for (auto region : record.MappingRegions)
		{
			const char *kind;

			switch (region.Kind)
			{
				case coverage::CounterMappingRegion::CodeRegion:
					kind = "code";
				break;
				case coverage::CounterMappingRegion::SkippedRegion:
					kind = "skip";
				break;
				case coverage::CounterMappingRegion::ExpansionRegion:
					kind = "expansion";
				break;
				#if 0
					case coverage::CounterMappingRegion::GapRegion:
						kind = "gap";
					break;
				#endif
				default:
					kind = "unknown";
				break;
			}

			PyObj range_tuple = Py_BuildValue("kkkkkks",
				(unsigned long) region.LineStart, (unsigned long) region.ColumnStart,
				(unsigned long) region.LineEnd, (unsigned long) region.ColumnEnd,
				(unsigned long) region.ExpandedFileID,
				(unsigned long) region.FileID, kind);
			PyList_Append(subseq, range_tuple);
			Py_DECREF(range_tuple);
		}
	}
	ITER_CR_CLOSE()

	return(0);
}

/**
	# Retrieve the instrumented source files.
*/
int
_list_source_files(PyObj seq, const char *object, const char *arch)
{
	auto CounterMappingBuff = MemoryBuffer::getFile(object);
	if (std::error_code EC = CounterMappingBuff.getError())
	{
		PyErr_SetString(PyExc_RuntimeError, EC.message().c_str());
		return(1);
	}

	POSTv9(SmallVector<std::unique_ptr<MemoryBuffer>, 4> bufs);

	auto CoverageReaderOrErr = CREATE_READER(CounterMappingBuff.get(), arch, bufs);
	if (!CoverageReaderOrErr)
	{
		if (auto E = CRE_GET_ERROR(CoverageReaderOrErr))
		{
			PyErr_SetString(PyExc_RuntimeError, ERR_STRING(E));
			return(1);
		}

		return(1);
	}

	std::set<std::string> paths;

	/*
		# The nested for loop will start a new section every time the fileid
		# changes so the reader can properly associate ranges.
	*/

	ITER_CR_RECORDS(R, CoverageReaderOrErr.get())
	{
		if (auto E = CMR_GET_ERROR(R))
			continue;

		const auto &record = RECORD(R);

		for (const auto path : record.Filenames)
		{
			/*
				# Usually one per function.
			*/
			paths.insert((std::string) path);
		}
	}
	ITER_CR_CLOSE()

	for (auto path : paths)
	{
		PyObj s;
		s = PyUnicode_FromStringAndSize(path.data(), path.length());
		if (s == NULL)
			return(1);
		PyList_Append(seq, s);
	}

	return(0);
}

/**
	# Load a CoverageMapping and get the per-file coverage information.
*/
int
_counters(PyObj seq, char *object, char *profile, char *arch)
{
	auto CoverageOrErr = CM_LOAD(object, profile, arch);

	if (auto E = CRE_GET_ERROR(CoverageOrErr))
	{
		PyErr_SetString(PyExc_RuntimeError, ERR_STRING(E));
		return(1);
	}

	auto coverage = std::move(CoverageOrErr.get());
	auto files = coverage.get()->getUniqueSourceFiles();

	for (auto &file : files)
	{
		auto data = coverage.get()->getCoverageForFile(file);
		std::string filestr = file;

		if (data.empty())
			continue;

		PyObj subseq, path_ob;

		path_ob = PyUnicode_FromStringAndSize(filestr.data(), filestr.size());
		subseq = PyList_New(0);
		PyObj x = PyTuple_New(2);
		PyTuple_SET_ITEM(x, 0, path_ob);
		PyTuple_SET_ITEM(x, 1, subseq);

		for (auto seg : data)
		{
			if (seg.HasCount && seg.IsRegionEntry && seg.Count > 0)
			{
				PyObj counter = Py_BuildValue("kkk",
					(unsigned long) seg.Line, (unsigned long) seg.Col,
					(unsigned long) seg.Count);
				PyList_Append(subseq, counter);
				Py_DECREF(counter);
			}
		}

		if (PyList_GET_SIZE(subseq) > 0)
			PyList_Append(seq, x);
		Py_DECREF(x);
	}

	return(0);
}

} // extern "C"
