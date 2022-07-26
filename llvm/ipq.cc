/**
	// Extract sources, regions, and counters from LLVM instrumented binaries and profile data files.
*/
#include <stddef.h>
#include <limits.h>

#define __STDC_LIMIT_MACROS 1
#define __STDC_CONSTANT_MACROS 1

#include <TargetConditionals.h>
#include <ctype.h>
#include <stdio.h>

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/Optional.h>
#include <llvm/ADT/SmallBitVector.h>

/*
	// CounterMappingRegion (mapping stored in binaries)
*/
#if (LLVM_VERSION_MAJOR == 3) && (LLVM_VERSION_MINOR < 9)
	#include <llvm/ProfileData/CoverageMapping.h>
	#include <llvm/ProfileData/CoverageMappingReader.h>
#else
	#include <llvm/ProfileData/Coverage/CoverageMapping.h>
	#include <llvm/ProfileData/Coverage/CoverageMappingReader.h>
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

#include <llvm/ProfileData/InstrProfReader.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/Errc.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <system_error>
#include <tuple>
#include <iostream>
#include <set>

using namespace llvm;

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
	// Identify the counts associated with the syntax areas.
*/
int
print_counters(FILE *fp, char *arch, char *object, char *datafile)
{
	auto mapping = CM_LOAD(object, datafile, arch);

	if (auto E = CRE_GET_ERROR(mapping))
	{
		fprintf(stderr, "%s\n", ERR_STRING(E));
		return(1);
	}

	auto coverage = std::move(mapping.get());
	auto files = coverage.get()->getUniqueSourceFiles();

	for (auto &file : files)
	{
		auto data = coverage.get()->getCoverageForFile(file);
		if (data.empty())
			continue;

		fprintf(fp, "@%.*s\n", (int) file.size(), file.data());

		for (auto seg : data)
		{
			if (seg.HasCount && seg.IsRegionEntry && seg.Count > 0)
			{
				fprintf(fp, "%u %u %llu\n", seg.Line, seg.Col, seg.Count);
			}
		}
	}

	return(0);
}

/**
	// Identify the regions of the sources that may have counts.
*/
int
print_regions(FILE *fp, char *arch, char *object)
{
	int last = -1;
	auto CounterMappingBuff = MemoryBuffer::getFile(object);

	if (std::error_code EC = CounterMappingBuff.getError())
	{
		char *err = (char *) EC.message().c_str();
		fprintf(stderr, "%s\n", err);
		return(1);
	}

	POSTv9(SmallVector<std::unique_ptr<MemoryBuffer>, 4> bufs);

	auto CoverageReaderOrErr = CREATE_READER(CounterMappingBuff.get(), arch, bufs);
	if (!CoverageReaderOrErr)
	{
		if (auto E = CRE_GET_ERROR(CoverageReaderOrErr))
		{
			fprintf(stderr, "%s\n", ERR_STRING(E));
			return(1);
		}

		fprintf(stderr, "failed to load counter mapping reader from object\n");
		return(1);
	}

	ITER_CR_RECORDS(R, CoverageReaderOrErr.get())
	{
		if (auto E = CMR_GET_ERROR(R))
			continue;

		const auto &record = RECORD(R);
		auto fname = record.FunctionName;

		fprintf(fp, "@%.*s\n", (int) fname.size(), fname.data());

		for (auto region : record.MappingRegions)
		{
			const char *kind;
			int ksz = 1;
			auto fi = region.FileID;
			auto fn = record.Filenames[fi];

			if (fi != last)
			{
				fprintf(fp, "%lu:%.*s\n", fi, (int) fn.size(), fn.data());
				last = fi;
			}

			switch (region.Kind)
			{
				case coverage::CounterMappingRegion::CodeRegion:
					ksz = 1;
					kind = "+";
				break;
				case coverage::CounterMappingRegion::SkippedRegion:
					ksz = 1;
					kind = "-";
				break;
				case coverage::CounterMappingRegion::ExpansionRegion:
					kind = "X";
					kind = record.Filenames[region.ExpandedFileID].data();
					ksz = record.Filenames[region.ExpandedFileID].size();
				break;
				case coverage::CounterMappingRegion::GapRegion:
					ksz = 1;
					kind = ".";
				break;
				default:
					ksz = 1;
					kind = "U";
				break;
			}

			fprintf(fp, "%lu %lu %lu %lu %.*s\n",
				(unsigned long) region.LineStart,
				(unsigned long) region.ColumnStart,
				(unsigned long) region.LineEnd,
				(unsigned long) region.ColumnEnd, ksz, kind);
		}
	}
	ITER_CR_CLOSE()

	return(0);
}

/**
	// Identify the set of source files associated with an image.
*/
int
print_sources(FILE *fp, char *arch, char *object)
{
	auto CounterMappingBuff = MemoryBuffer::getFile(object);

	if (std::error_code EC = CounterMappingBuff.getError())
	{
		char *err;
		err = (char *) EC.message().c_str();
		fprintf(stderr, "%s\n", err);
		return(1);
	}

	POSTv9(SmallVector<std::unique_ptr<MemoryBuffer>, 4> bufs);

	auto CoverageReaderOrErr = CREATE_READER(CounterMappingBuff.get(), arch, bufs);
	if (!CoverageReaderOrErr)
	{
		if (auto E = CRE_GET_ERROR(CoverageReaderOrErr))
			fprintf(stderr, "%s\n", ERR_STRING(E));
		else
			fprintf(stderr, "unknown error\n");

		return(1);
	}

	std::set<std::string> paths;

	/*
		// The nested for loop will start a new section every time the fileid
		// changes so the reader can properly associate ranges.
	*/

	ITER_CR_RECORDS(R, CoverageReaderOrErr.get())
	{
		if (auto E = CMR_GET_ERROR(R))
			continue;

		const auto &record = RECORD(R);

		for (const auto path : record.Filenames)
		{
			/*
				// Usually one per function.
			*/
			paths.insert((std::string) path);
		}
	}
	ITER_CR_CLOSE()

	for (auto path : paths)
	{
		fprintf(fp, "%.*s\n", (int) path.length(), path.data());
	}

	return(0);
}

int
main(int argc, char *argv[])
{
	if (strcmp(argv[1], "regions") == 0)
	{
		if (argc != 4)
			fprintf(stderr, "ERROR: regions requires exactly two arguments.\n", argv[1]);
		else
			return(print_regions(stdout, argv[2], argv[3]));
	}
	else if (strcmp(argv[1], "sources") == 0)
	{
		if (argc != 4)
			fprintf(stderr, "ERROR: sources requires exactly two arguments.\n", argv[1]);
		else
			return(print_sources(stdout, argv[2], argv[3]));
	}
	else
	{
		if (strcmp(argv[1], "counters") == 0)
		{
			if (argc != 5)
				fprintf(stderr, "ERROR: counters requires exactly three arguments.\n", argv[1]);
			else
				return(print_counters(stdout, argv[2], argv[3], argv[4]));
		}
		else
			fprintf(stderr, "unknown query '%s'\n", argv[1]);
	}

	return(1);
}
