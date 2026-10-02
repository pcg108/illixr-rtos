import sys,unittest,tempfile,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
import run_openblas_validation as stages
class StageGates(unittest.TestCase):
 def test_selected_firmware_identity(self):
  with tempfile.TemporaryDirectory() as d:
   w=Path(d);(w/'control').mkdir();elf=w/'selected.elf';elf.write_bytes(b'firmware')
   (w/'control/firmware-selection.json').write_text(json.dumps({'spike-single-openblas_scalar':dict(elf=str(elf),sha256=stages.fs.sha256(elf))}))
   self.assertEqual(stages.firmware(w,'spike',1,'openblas_scalar'),elf)
   elf.write_bytes(b'changed')
   with self.assertRaisesRegex(ValueError,'changed'):stages.firmware(w,'spike',1,'openblas_scalar')
 def test_partial_matrix_never_opens_gate(self):
  with tempfile.TemporaryDirectory() as d:
   w=Path(d);stages.save(w,'spike-scalar',[{'passed':True}]);self.assertFalse(json.loads((w/'runtime/spike-scalar.json').read_text())['all_passed'])
   with self.assertRaises(ValueError):stages.require_stage(w,'spike-scalar')
 def test_changed_firmware_closes_gate(self):
  with tempfile.TemporaryDirectory() as d:
   w=Path(d);elf=w/'test.elf';elf.write_bytes(b'firmware');items=[]
   for harts in (1,4):
    out=w/str(harts);out.mkdir();(out/'analysis.json').write_text(json.dumps({'passed':True,'complete':True}))
    items.append(dict(passed=True,harts=harts,backend='openblas_scalar',elf=str(elf),elf_sha256=stages.fs.sha256(elf),output=str(out)))
   stages.save(w,'spike-scalar',items);stages.require_stage(w,'spike-scalar')
   elf.write_bytes(b'changed')
   with self.assertRaisesRegex(ValueError,'Stale'):stages.require_stage(w,'spike-scalar')
 def test_incomplete_trace_closes_gate(self):
  with tempfile.TemporaryDirectory() as d:
   w=Path(d);elf=w/'test.elf';elf.write_bytes(b'firmware');out=w/'result';out.mkdir();(out/'analysis.json').write_text('{"passed":true,"complete":false}')
   stages.save(w,'spike-scalar',[dict(passed=True,harts=h,backend='openblas_scalar',elf=str(elf),elf_sha256=stages.fs.sha256(elf),output=str(out)) for h in (1,4)])
   with self.assertRaisesRegex(ValueError,'evidence'):stages.require_stage(w,'spike-scalar')
if __name__=='__main__': unittest.main()
