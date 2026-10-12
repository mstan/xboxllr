import importlib.util
import tempfile
import unittest
from pathlib import Path

spec=importlib.util.spec_from_file_location('capture',Path(__file__).resolve().parents[1]/'tools/native_audio_reference_capture.py')
capture=importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class ReferenceCaptureRateTest(unittest.TestCase):
    def test_created_rate_does_not_replace_actual_dma_rate(self):
        def record(kind,frame,rate,payload=b''):
            return capture.HEADER.pack(0x52414c58,1,kind,capture.HEADER.size,
                frame,1125000,1,4,frame,frame,rate,2,16,4,0,len(payload),0,0,0,4)+payload
        data=(record(3,0,44100,b'\0'*4)+record(1,1,48000,b'\1\2\3\4')+
              record(4,1,48000)+record(2,2,48000,b'\1\2\3\4')+
              record(1,3,22050,b'\5\6\7\10')+record(2,4,22050,b'\5\6\7\10')+record(6,5,22050))
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'engine-1.dma.bin';path.write_bytes(data)
            report,source,_=capture.inspect(path)
        voice=report['voices'][0]
        self.assertEqual(voice['rate'],44100)
        self.assertEqual(voice['rate_history'],[dict(frame=0,rate=44100),dict(frame=1,rate=48000),dict(frame=3,rate=22050)])
        self.assertEqual(voice['source_bytes_by_rate'],{'48000':4,'22050':4})
        self.assertEqual(source[4],b'\1\2\3\4\5\6\7\10')
        self.assertEqual(report['ring_byte_mismatches'],0)


if __name__=='__main__':
    unittest.main()
