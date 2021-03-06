#include <Python.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>
#endif

#include "PyScoreDraft.h"

#include <Note.h>
#include <Beat.h>
#include <SingingPiece.h>
#include <RapPiece.h>

#include <Instrument.h>
#include <Percussion.h>
#include <Singer.h>

#include <Deferred.h>
#include <TrackBuffer.h>
#include <instruments/PureSin.h>
#include <instruments/Square.h>
#include <instruments/Sawtooth.h>
#include <instruments/Triangle.h>
#include <instruments/NaivePiano.h>
#include <instruments/BottleBlow.h>

#include "WinWavWriter.h"

#include <vector>
#include <utility>
#include <string>
#include <string.h>
#include <stdio.h>

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif


class StdLogger : public Logger
{
public:
	virtual void PrintLine(const char* line) const
	{
		printf("%s\n", line);
	}
};

static StdLogger s_logger;
static PyScoreDraft s_PyScoreDraft;

static void s_RegisterDefaultClasses()
{
	static t_InstInitializer<PureSin> s_PureSin;
	s_PyScoreDraft.RegisterInstrumentClass("PureSin", &s_PureSin, "\t# A sin-wav generator\n");
	static t_InstInitializer<Square> s_Square;
	s_PyScoreDraft.RegisterInstrumentClass("Square", &s_Square, "\t# A square-wav generator\n");
	static t_InstInitializer<Triangle> s_Triangle;
	s_PyScoreDraft.RegisterInstrumentClass("Triangle", &s_Triangle, "\t# A triangle-wav generator\n");
	static t_InstInitializer<Sawtooth> s_Sawtooth;
	s_PyScoreDraft.RegisterInstrumentClass("Sawtooth", &s_Sawtooth, "\t# A sawtooth-wav generator\n");
	static t_InstInitializer<NaivePiano> s_NaivePiano;
	s_PyScoreDraft.RegisterInstrumentClass("NaivePiano", &s_NaivePiano, "\t# A naive piano tone by algebra formulas\n" );
	static t_InstInitializer<BottleBlow> s_BottleBlow;
	s_PyScoreDraft.RegisterInstrumentClass("BottleBlow", &s_BottleBlow, "\t# A bottle-blow tone using a noise signal passing a BPF\n");

}

static PyObject* ScanExtensions(PyObject *self, PyObject *args)
{
	const char* root;
	if (!PyArg_ParseTuple(args, "s", &root))
		return PyLong_FromLong(0);

#ifdef _WIN32
	WIN32_FIND_DATAA ffd;
	HANDLE hFind = INVALID_HANDLE_VALUE;

	char extSearchStr[1024];
	sprintf(extSearchStr, "%s/Extensions/*.dll", root);

	hFind = FindFirstFileA(extSearchStr, &ffd);
	if (INVALID_HANDLE_VALUE == hFind) return PyLong_FromLong(0);

	do
	{
		if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

		char path[1024];
		sprintf(path, "%s/Extensions/%s", root, ffd.cFileName);

		HINSTANCE hinstLib;
		hinstLib = LoadLibraryA(path);

		if (hinstLib != NULL)
		{
			typedef void (InitializeFunc)(PyScoreDraft* pyScoreDraft, const char* root);
			InitializeFunc* initFunc = (InitializeFunc*)GetProcAddress(hinstLib, "Initialize");
			if (initFunc != NULL)
			{
				printf("Loading extension: %s\n", ffd.cFileName);
				initFunc(&s_PyScoreDraft, root);
			}
		}

	} while (FindNextFile(hFind, &ffd) != 0);
#else
	DIR *dir;
	struct dirent *entry;

	char extPath[1024];
	sprintf(extPath, "%s/Extensions", root);

	if (dir = opendir(extPath))
	{
		while ((entry = readdir(dir)) != NULL)
		{
			const char* ext = entry->d_name + strlen(entry->d_name) - 3;
			if (strcmp(ext, ".so") == 0)
			{
				char path[1024];
				sprintf(path, "%s/Extensions/%s", root, entry->d_name);

				void *handle = dlopen(path, RTLD_LAZY);
				if (handle)
				{
					dlerror();
					typedef void (InitializeFunc)(PyScoreDraft* pyScoreDraft, const char* root);
					InitializeFunc* initFunc;
					*(void **)(&initFunc) = dlsym(handle, "Initialize");
					if (!dlerror())
					{
						printf("Loading extension: %s\n", entry->d_name);
						initFunc(&s_PyScoreDraft, root);
					}

				}

			}
		}
	}

#endif


	return PyLong_FromLong(0);
}

static PyObject* GenerateCode(PyObject *self, PyObject *args)
{
	std::string generatedCode = "";
	std::string summary = "";
	
	// Instruments 
	generatedCode += "# PyScoreDraft Generated Code\n\n";
	generatedCode += "# Instruments\n\n";

	summary += "=====================================\n";
	summary += "PyScoreDraft Generated Code - Summary\n";
	summary += "=====================================\n\n";
	summary += "Instruments:\n";

	unsigned count = s_PyScoreDraft.NumOfIntrumentClasses();
	for (unsigned i = 0; i < count; i++)
	{
		InstrumentClass instCls = s_PyScoreDraft.GetInstrumentClass(i);
		generatedCode +=
			std::string("def ") + instCls.m_name.data() + "():\n"
			+ instCls.m_comment +
			+ "\treturn Instrument(" + std::to_string(i) + ")\n\n";

		summary += std::to_string(i) + ": " + s_PyScoreDraft.GetInstrumentClass(i).m_name.data()+"\n";
	}
	summary += "\n";

	//Percussions
	generatedCode += "# Percussions\n\n";
	summary += "Percussions:\n";

	count = s_PyScoreDraft.NumOfPercussionClasses();
	for (unsigned i = 0; i < count; i++)
	{
		PercussionClass percCls = s_PyScoreDraft.GetPercussionClass(i);
		generatedCode +=
			std::string("def ") + percCls.m_name.data() + "():\n"
			+ percCls.m_comment +
			+ "\treturn Percussion(" + std::to_string(i) + ")\n\n";

		summary += std::to_string(i) + ": " + s_PyScoreDraft.GetPercussionClass(i).m_name.data() + "\n";
	}
	summary += "\n";

	//Singers
	generatedCode += "# Singers\n\n";
	summary += "Singers:\n";

	count = s_PyScoreDraft.NumOfSingerClasses();
	for (unsigned i = 0; i < count; i++)
	{
		SingerClass singerCls = s_PyScoreDraft.GetSingerClass(i);
		generatedCode +=
			std::string("def ") + singerCls.m_name.data() + "():\n"
			+ singerCls.m_comment +
			+ "\treturn Singer(" + std::to_string(i) + ")\n\n";

		summary += std::to_string(i) + ": " + s_PyScoreDraft.GetSingerClass(i).m_name.data() + "\n";
	}
	summary += "\n";

	//Interfaces
	generatedCode += "# Interfaces\n\n";
	summary += "Interfaces:\n";
	
	count = s_PyScoreDraft.NumOfInterfaceExtensions();
	for (unsigned i = 0; i < count; i++)
	{
		InterfaceExtension ext = s_PyScoreDraft.GetInterfaceExtension(i);

		generatedCode +=
			std::string("def ") + ext.m_name + "(" + ext.m_input_params + "):\n"
			+ ext.m_comment
			+ "\treturn PyScoreDraft.CallExtension(" + std::to_string(i);

		if (ext.m_call_params != "") generatedCode += ",(" + ext.m_call_params + ")";

		generatedCode += ")\n\n";

		summary += std::to_string(i) + ": " + ext.m_name + "\n";
	}
	summary += "\n";

	PyObject* list = PyList_New(0);
	PyList_Append(list, _PyUnicode_FromASCII(generatedCode.data(), generatedCode.length()));
	PyList_Append(list, _PyUnicode_FromASCII(summary.data(), summary.length()));

	return list;

}

static PyObject* InitTrackBuffer(PyObject *self, PyObject *args)
{
	unsigned chn;
	if (!PyArg_ParseTuple(args, "I", &chn))
		return NULL;

	TrackBuffer_deferred buffer(44100,chn);
	unsigned id = s_PyScoreDraft.AddTrackBuffer(buffer);
	return PyLong_FromUnsignedLong((unsigned long)(id));
}

static PyObject* DelTrackBuffer(PyObject *self, PyObject *args)
{
	unsigned BufferId;
	if (!PyArg_ParseTuple(args, "I", &BufferId))
		return NULL;

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(BufferId);
	buffer.Abondon();

	return PyLong_FromLong(0);
}

static PyObject* InitInstrument(PyObject *self, PyObject *args)
{
	unsigned clsId;
	if (!PyArg_ParseTuple(args, "I", &clsId))
		return NULL;

	InstrumentClass InstCls = s_PyScoreDraft.GetInstrumentClass(clsId);
	Instrument_deferred inst = InstCls.m_initializer->Init();
	unsigned id = s_PyScoreDraft.AddInstrument(inst);

	return PyLong_FromUnsignedLong(id);
}

static PyObject* DelInstrument(PyObject *self, PyObject *args)
{
	unsigned InstrumentId;
	if (!PyArg_ParseTuple(args, "I", &InstrumentId))
		return NULL;

	Instrument_deferred instrument = s_PyScoreDraft.GetInstrument(InstrumentId);
	instrument.Abondon();

	return PyLong_FromLong(0);
}

static PyObject* InitPercussion(PyObject *self, PyObject *args)
{
	unsigned clsId;
	if (!PyArg_ParseTuple(args, "I", &clsId))
		return NULL;

	PercussionClass PercCls = s_PyScoreDraft.GetPercussionClass(clsId);
	Percussion_deferred perc = PercCls.m_initializer->Init();
	unsigned id = s_PyScoreDraft.AddPercussion(perc);

	return PyLong_FromUnsignedLong(id);
}

static PyObject* DelPercussion(PyObject *self, PyObject *args)
{
	unsigned PercussionId;
	if (!PyArg_ParseTuple(args, "I", &PercussionId))
		return NULL;

	Percussion_deferred perc = s_PyScoreDraft.GetPercussion(PercussionId);
	perc.Abondon();

	return PyLong_FromLong(0);
}

static PyObject* InitSinger(PyObject *self, PyObject *args)
{
	unsigned clsId;
	if (!PyArg_ParseTuple(args, "I", &clsId))
		return NULL;

	SingerClass SingerCls = s_PyScoreDraft.GetSingerClass(clsId);
	Singer_deferred singer = SingerCls.m_initializer->Init();
	unsigned id = s_PyScoreDraft.AddSinger(singer);

	return PyLong_FromUnsignedLong(id);
}

static PyObject* DelSinger(PyObject *self, PyObject *args)
{
	unsigned SingerId;
	if (!PyArg_ParseTuple(args, "I", &SingerId))
		return NULL;

	Singer_deferred singer = s_PyScoreDraft.GetSinger(SingerId);
	singer.Abondon();

	return PyLong_FromLong(0);
}

static PyObject* TrackBufferSetVolume(PyObject *self, PyObject *args)
{
	unsigned BufferId;
	float volume;
	if (!PyArg_ParseTuple(args, "If", &BufferId, &volume))
		return NULL;

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(BufferId);
	buffer->SetVolume(volume);

	return PyLong_FromLong(0);
}

static PyObject* TrackBufferGetVolume(PyObject *self, PyObject *args)
{
	unsigned BufferId;
	if (!PyArg_ParseTuple(args, "I", &BufferId))
		return NULL;

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(BufferId);
	return PyFloat_FromDouble((double)buffer->Volume());
}


static PyObject* TrackBufferSetPan(PyObject *self, PyObject *args)
{
	unsigned BufferId;
	float pan;
	if (!PyArg_ParseTuple(args, "If", &BufferId, &pan))
		return NULL;

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(BufferId);
	buffer->SetPan(pan);

	return PyLong_FromLong(0);
}

static PyObject* TrackBufferGetPan(PyObject *self, PyObject *args)
{
	unsigned BufferId;
	if (!PyArg_ParseTuple(args, "I", &BufferId))
		return NULL;

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(BufferId);
	return PyFloat_FromDouble((double)buffer->Pan());
}


static PyObject* TrackBufferGetNumberOfSamples(PyObject *self, PyObject *args)
{
	unsigned BufferId;
	if (!PyArg_ParseTuple(args, "I", &BufferId))
		return NULL;

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(BufferId);
	return PyLong_FromLong((long)buffer->NumberOfSamples());
}

static PyObject* TrackBufferGetNumberOfChannels(PyObject *self, PyObject *args)
{
	unsigned BufferId;
	if (!PyArg_ParseTuple(args, "I", &BufferId))
		return NULL;

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(BufferId);
	return PyLong_FromLong((long)buffer->NumberOfChannels());
}

static PyObject* TrackBufferGetCursor(PyObject *self, PyObject *args)
{
	unsigned BufferId;
	if (!PyArg_ParseTuple(args, "I", &BufferId))
		return NULL;

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(BufferId);
	return PyFloat_FromDouble((double)buffer->GetCursor());
}

static PyObject* TrackBufferSetCursor(PyObject *self, PyObject *args)
{
	unsigned BufferId;
	float cursor;
	if (!PyArg_ParseTuple(args, "If", &BufferId, &cursor))
		return NULL;

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(BufferId);
	buffer->SetCursor(cursor);

	return PyLong_FromLong(0);
}

static PyObject* TrackBufferMoveCursor(PyObject *self, PyObject *args)
{
	unsigned BufferId;
	float cursor_delta;
	if (!PyArg_ParseTuple(args, "If", &BufferId, &cursor_delta))
		return NULL;

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(BufferId);
	buffer->MoveCursor(cursor_delta);

	return PyLong_FromLong(0);
}

static PyObject* InstrumentPlay(PyObject *self, PyObject *args)
{
	unsigned TrackBufferId = (unsigned)PyLong_AsUnsignedLong(PyTuple_GetItem(args, 0));
	unsigned InstrumentId = (unsigned)PyLong_AsUnsignedLong(PyTuple_GetItem(args, 1));
	PyObject *seq_py = PyTuple_GetItem(args, 2);
	unsigned tempo = (unsigned)PyLong_AsUnsignedLong(PyTuple_GetItem(args, 3));
	float RefFreq = (float)PyFloat_AsDouble(PyTuple_GetItem(args, 4));

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(TrackBufferId);
	Instrument_deferred instrument = s_PyScoreDraft.GetInstrument(InstrumentId);

	size_t piece_count = PyList_Size(seq_py);
	for (size_t i = 0; i < piece_count; i++)
	{
		PyObject *item = PyList_GetItem(seq_py, i);
		if (PyObject_TypeCheck(item, &PyTuple_Type))
		{
			PyObject* _item = PyTuple_GetItem(item, 0);
			if (PyObject_TypeCheck(_item, &PyUnicode_Type)) // singing
			{
				size_t tupleSize = PyTuple_Size(item);

				size_t j = 0;
				while (j < tupleSize)
				{
					j++; // by-pass lyric
					_item = PyTuple_GetItem(item, j);
					if (PyObject_TypeCheck(_item, &PyTuple_Type)) // singing note
					{
						for (; j<tupleSize; j++)
						{
							_item = PyTuple_GetItem(item, j);
							if (!PyObject_TypeCheck(_item, &PyTuple_Type)) break;

							Note note;
							note.m_freq_rel = (float)PyFloat_AsDouble(PyTuple_GetItem(_item, 0));
							note.m_duration = (int)PyLong_AsLong(PyTuple_GetItem(_item, 1));
					
							instrument->PlayNote(*buffer, note, tempo, RefFreq);
						}
					}
					else if (PyObject_TypeCheck(_item, &PyLong_Type)) // singing rap
					{
						int duration = (int)PyLong_AsLong(PyTuple_GetItem(item, j));
						Note note;
						note.m_freq_rel = (float)PyFloat_AsDouble(PyTuple_GetItem(item, j + 1));
						note.m_duration = duration;
						instrument->PlayNote(*buffer, note, tempo, RefFreq);

						j++; // at freq1
						j++; // at freq2
						j++; // at next
					}
				}
			}
			else if (PyObject_TypeCheck(_item, &PyFloat_Type)) // note
			{
				Note note;
				note.m_freq_rel = (float)PyFloat_AsDouble(PyTuple_GetItem(item, 0));
				note.m_duration = (int)PyLong_AsLong(PyTuple_GetItem(item, 1));

				instrument->PlayNote(*buffer, note, tempo, RefFreq);
			}
		}
		else if (PyObject_TypeCheck(item, &PyUnicode_Type))
		{
			instrument->Tune(_PyUnicode_AsString(item));
		}
	}

	return PyLong_FromUnsignedLong(0);
}

static PyObject* InstrumentTune(PyObject *self, PyObject *args)
{
	unsigned InstrumentId;
	const char* cmd;

	if (!PyArg_ParseTuple(args, "Is", &InstrumentId, &cmd))
		return NULL;

	Instrument_deferred instrument = s_PyScoreDraft.GetInstrument(InstrumentId);
	instrument->Tune(cmd);
	return PyLong_FromLong(0);
}

static PyObject* PercussionPlay(PyObject *self, PyObject *args)
{
	unsigned TrackBufferId = (unsigned)PyLong_AsUnsignedLong(PyTuple_GetItem(args, 0));
	PyObject *percId_list = PyTuple_GetItem(args, 1);
	PyObject *seq_py = PyTuple_GetItem(args, 2);
	unsigned tempo = (unsigned)PyLong_AsUnsignedLong(PyTuple_GetItem(args, 3));

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(TrackBufferId);

	size_t perc_count = PyList_Size(percId_list);
	Percussion_deferred *perc_List = new Percussion_deferred[perc_count];
	for (size_t i = 0; i < perc_count; i++)
	{
		unsigned long percId = PyLong_AsUnsignedLong(PyList_GetItem(percId_list, i));
		perc_List[i] = s_PyScoreDraft.GetPercussion(percId);
	}

	size_t beat_count = PyList_Size(seq_py);
	for (size_t i = 0; i < beat_count; i++)
	{
		PyObject *item = PyList_GetItem(seq_py, i);
		int percId = (int)PyLong_AsLong(PyTuple_GetItem(item, 0));

		PyObject *operation = PyTuple_GetItem(item, 1);
		if (PyObject_TypeCheck(operation, &PyLong_Type))
		{
			int duration = (int)PyLong_AsLong(operation);

			if (percId >= 0)
				perc_List[percId]->PlayBeat(*buffer, duration, tempo);
			else if (duration >= 0)
				Percussion::PlaySilence(*buffer, duration, tempo);
			else
				Percussion::PlayBackspace(*buffer, -duration, tempo);
		}
		else if (PyObject_TypeCheck(operation, &PyUnicode_Type))
		{
			perc_List[percId]->Tune(_PyUnicode_AsString(operation));
		}
	}

	delete[] perc_List;

	return PyLong_FromUnsignedLong(0);
}

static PyObject* PercussionTune(PyObject *self, PyObject *args)
{
	unsigned PercussionId;
	const char* cmd;

	if (!PyArg_ParseTuple(args, "Is", &PercussionId, &cmd))
		return NULL;

	Percussion_deferred perc = s_PyScoreDraft.GetPercussion(PercussionId);
	perc->Tune(cmd);
	return PyLong_FromLong(0);
}

static PyObject* Sing(PyObject *self, PyObject *args)
{
	unsigned TrackBufferId = (unsigned)PyLong_AsUnsignedLong(PyTuple_GetItem(args, 0));
	unsigned SingerId = (unsigned)PyLong_AsUnsignedLong(PyTuple_GetItem(args, 1));
	PyObject *seq_py = PyTuple_GetItem(args, 2);
	unsigned tempo = (unsigned)PyLong_AsUnsignedLong(PyTuple_GetItem(args, 3));
	float RefFreq = (float)PyFloat_AsDouble(PyTuple_GetItem(args, 4));

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(TrackBufferId);

	Singer_deferred singer = s_PyScoreDraft.GetSinger(SingerId);
	std::string lyric_charset = singer->GetLyricCharset();

	size_t piece_count = PyList_Size(seq_py);

	for (size_t i = 0; i < piece_count; i++)
	{
		PyObject *item = PyList_GetItem(seq_py, i);
		if (PyObject_TypeCheck(item, &PyTuple_Type))
		{
			PyObject *_item = PyTuple_GetItem(item, 0);
			if (PyObject_TypeCheck(_item, &PyUnicode_Type)) // singing
			{
				SingingSequence singing_pieces;
				RapSequence rap_pieces;

				size_t tupleSize = PyTuple_Size(item);

				size_t j = 0;
				while (j < tupleSize)
				{
					_item = PyTuple_GetItem(item, j);
					PyObject *byteCode = PyUnicode_AsEncodedString(_item, lyric_charset.data(), 0);
					std::string lyric = PyBytes_AS_STRING(byteCode);
					j++;

					_item = PyTuple_GetItem(item, j);
					if (PyObject_TypeCheck(_item, &PyTuple_Type)) // singing note
					{
						SingingPiece piece;
						piece.m_lyric = lyric;

						for (; j<tupleSize; j++)
						{
							_item = PyTuple_GetItem(item, j);
							if (!PyObject_TypeCheck(_item, &PyTuple_Type)) break;					

							Note note;
							note.m_freq_rel = (float)PyFloat_AsDouble(PyTuple_GetItem(_item, 0));
							note.m_duration = (int)PyLong_AsLong(PyTuple_GetItem(_item, 1));
							piece.m_notes.push_back(note);
							
						}
						singing_pieces.push_back(piece);
					}
					else if (PyObject_TypeCheck(_item, &PyLong_Type)) // singing rap
					{
						RapPiece piece;
						piece.m_lyric = lyric;
						piece.m_duration = (int)PyLong_AsLong(PyTuple_GetItem(item, j));
						j++;
						piece.m_freq1 = (float)PyFloat_AsDouble(PyTuple_GetItem(item, j));
						j++;
						piece.m_freq2 = (float)PyFloat_AsDouble(PyTuple_GetItem(item, j));
						j++;
						rap_pieces.push_back(piece);
					}

				}
				if (singing_pieces.size() > 0)
				{
					if (singing_pieces.size() < 2)
					{
						singer->SingPiece(*buffer, singing_pieces[0], tempo, RefFreq);
					}
					else
					{
						singer->SingConsecutivePieces(*buffer, singing_pieces, tempo, RefFreq);
					}
				}
				if (rap_pieces.size() > 0)
				{
					if (rap_pieces.size() < 2)
					{
						singer->RapAPiece(*buffer, rap_pieces[0], tempo, RefFreq);
					}
					else
					{
						singer->RapConsecutivePieces(*buffer, rap_pieces, tempo, RefFreq);
					}
				}
				
			}
			else if (PyObject_TypeCheck(_item, &PyFloat_Type)) // note
			{
				SingingPiece piece;
				piece.m_lyric = "";

				Note note;
				note.m_freq_rel = (float)PyFloat_AsDouble(PyTuple_GetItem(item, 0));
				note.m_duration = (int)PyLong_AsLong(PyTuple_GetItem(item, 1));

				piece.m_notes.push_back(note);
				singer->SingPiece(*buffer, piece, tempo, RefFreq);
			}

		}
		else if (PyObject_TypeCheck(item, &PyUnicode_Type))
		{
			singer->Tune(_PyUnicode_AsString(item));
		}
	}

	return PyLong_FromUnsignedLong(0);
}

static PyObject* SingerTune(PyObject *self, PyObject *args)
{
	unsigned SingerId;
	const char* cmd;

	if (!PyArg_ParseTuple(args, "Is", &SingerId, &cmd))
		return NULL;

	Singer_deferred singer = s_PyScoreDraft.GetSinger(SingerId);
	singer->Tune(cmd);
	return PyLong_FromLong(0);
}

static PyObject* MixTrackBufferList(PyObject *self, PyObject *args)
{
	unsigned TargetTrackBufferId = (unsigned)PyLong_AsUnsignedLong(PyTuple_GetItem(args, 0));
	PyObject *list = PyTuple_GetItem(args, 1);

	TrackBuffer_deferred targetBuffer = s_PyScoreDraft.GetTrackBuffer(TargetTrackBufferId);
	
	size_t bufferCount = PyList_Size(list);
	TrackBuffer_deferred* bufferList = new TrackBuffer_deferred[bufferCount];
	for (size_t i = 0; i < bufferCount; i++)
	{
		unsigned long listId = PyLong_AsUnsignedLong(PyList_GetItem(list, i));
		bufferList[i] = s_PyScoreDraft.GetTrackBuffer(listId);
	}

	targetBuffer->CombineTracks((unsigned)bufferCount, bufferList);
	delete[] bufferList;

	return PyLong_FromUnsignedLong(0);
}

static PyObject* WriteTrackBufferToWav(PyObject *self, PyObject *args)
{
	unsigned BufferId;
	const char* fn;
	if (!PyArg_ParseTuple(args, "Is", &BufferId, &fn))
		return NULL;

	TrackBuffer_deferred buffer = s_PyScoreDraft.GetTrackBuffer(BufferId);
	WriteToWav(*buffer, fn);

	return PyLong_FromUnsignedLong(0);
}

static PyObject* CallExtension(PyObject *self, PyObject *args)
{
	unsigned extId = (unsigned)PyLong_AsUnsignedLong(PyTuple_GetItem(args, 0));
	PyObject* params;
	if (PyTuple_Size(args)<2) params = PyTuple_New(0);
	else params = PyTuple_GetItem(args, 1);

	InterfaceExtension ext = s_PyScoreDraft.GetInterfaceExtension(extId);
	PyObject* ret=ext.m_func(params);
	
	return ret;
}

static PyObject* TellDuration(PyObject *self, PyObject *args)
{
	PyObject *seq_py = PyTuple_GetItem(args, 0);
	size_t piece_count = PyList_Size(seq_py);

	unsigned dure = 0;
	for (size_t i = 0; i < piece_count; i++)
	{
		PyObject *item = PyList_GetItem(seq_py, i);
		if (PyObject_TypeCheck(item, &PyTuple_Type))
		{
			PyObject* _item = PyTuple_GetItem(item, 0);
			if (PyObject_TypeCheck(_item, &PyUnicode_Type)) // singing
			{
				size_t tupleSize = PyTuple_Size(item);
				size_t j = 0;
				while (j < tupleSize)
				{
					j++; // by-pass lyric
					_item = PyTuple_GetItem(item, j);
					if (PyObject_TypeCheck(_item, &PyTuple_Type)) // singing note
					{
						for (; j<tupleSize; j++)
						{
							_item = PyTuple_GetItem(item, j);
							if (!PyObject_TypeCheck(_item, &PyTuple_Type)) break;
							dure+= (int)PyLong_AsLong(PyTuple_GetItem(_item, 1));
						}
					}
					else if (PyObject_TypeCheck(_item, &PyLong_Type)) // singing rap
					{
						dure += (int)PyLong_AsLong(PyTuple_GetItem(item, j));
						j++; // at freq1
						j++; // at freq2
						j++; // at next
					}
				}

			}
			else if (PyObject_TypeCheck(_item, &PyFloat_Type)) // note
			{
				dure += (int)PyLong_AsLong(PyTuple_GetItem(item, 1));
			}
			else if (PyObject_TypeCheck(_item, &PyLong_Type)) // beat
			{
				dure += (int)PyLong_AsLong(PyTuple_GetItem(item, 1));
			}
		}
	}

	return PyLong_FromUnsignedLong(dure);

}

static PyMethodDef s_PyScoreDraftMethods[] = {
	{
		"ScanExtensions",
		ScanExtensions,
		METH_VARARGS,
		""
	},
	{
		"GenerateCode",
		GenerateCode,
		METH_VARARGS,
		""
	},
	{
		"InitTrackBuffer",
		InitTrackBuffer,
		METH_VARARGS,
		""
	},
	{
		"DelTrackBuffer",
		DelTrackBuffer,
		METH_VARARGS,
		""
	},
	{
		"InitInstrument",
		InitInstrument,
		METH_VARARGS,
		""
	},
	{
		"DelInstrument",
		DelInstrument,
		METH_VARARGS,
		""
	},
	{
		"InitPercussion",
		InitPercussion,
		METH_VARARGS,
		""
	},
	{
		"DelPercussion",
		DelPercussion,
		METH_VARARGS,
		""
	},
	{
		"InitSinger",
		InitSinger,
		METH_VARARGS,
		""
	},
	{
		"DelSinger",
		DelSinger,
		METH_VARARGS,
		""
	},
	{
		"TrackBufferSetVolume",
		TrackBufferSetVolume,
		METH_VARARGS,
		""
	},
	{
		"TrackBufferGetVolume",
		TrackBufferGetVolume,
		METH_VARARGS,
		""
	},
	{
		"TrackBufferSetPan",
		TrackBufferSetPan,
		METH_VARARGS,
		""
	},
	{
		"TrackBufferGetPan",
		TrackBufferGetPan,
		METH_VARARGS,
		""
	},
	{
		"TrackBufferGetNumberOfSamples",
		TrackBufferGetNumberOfSamples,
		METH_VARARGS,
		""
	},
	{
		"TrackBufferGetNumberOfChannels",
		TrackBufferGetNumberOfChannels,
		METH_VARARGS,
		""
	},
	{
		"TrackBufferGetCursor",
		TrackBufferGetCursor,
		METH_VARARGS,
		""
	},
	{
		"TrackBufferSetCursor",
		TrackBufferSetCursor,
		METH_VARARGS,
		""
	},
	{
		"TrackBufferMoveCursor",
		TrackBufferMoveCursor,
		METH_VARARGS,
		""
	},
	{
		"InstrumentPlay",
		InstrumentPlay,
		METH_VARARGS,
		""
	},
	{
		"InstrumentTune",
		InstrumentTune,
		METH_VARARGS,
		""
	},
	{
		"PercussionPlay",
		PercussionPlay,
		METH_VARARGS,
		""
	},
	{
		"PercussionTune",
		PercussionTune,
		METH_VARARGS,
		""
	},
	{
		"Sing",
		Sing,
		METH_VARARGS,
		""
	},
	{
		"SingerTune",
		SingerTune,
		METH_VARARGS,
		""
	},
	{
		"MixTrackBufferList",
		MixTrackBufferList,
		METH_VARARGS,
		""
	},
	{
		"WriteTrackBufferToWav",
		WriteTrackBufferToWav,
		METH_VARARGS,
		""
	},
	{
		"CallExtension",
		CallExtension,
		METH_VARARGS,
		""
	},
	{
		"TellDuration",
		TellDuration,
		METH_VARARGS,
		""
	},
	{ NULL, NULL, 0, NULL }
};

PyScoreDraft::PyScoreDraft()
{
	m_logger = nullptr; 
	m_PyScoreDraftMethods = s_PyScoreDraftMethods;
}

static struct PyModuleDef cModPyDem =
{
	PyModuleDef_HEAD_INIT,
	"PyScoreDraft_module", /* name of module */
	"",          /* module documentation, may be NULL */
	-1,          /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
	s_PyScoreDraftMethods
}; 

PyMODINIT_FUNC PyInit_PyScoreDraft(void) {
	s_RegisterDefaultClasses();
	return PyModule_Create(&cModPyDem);
}
