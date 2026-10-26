"""Guided offline workflow; no device access, implicit freeze or validated export."""
import argparse
from pathlib import Path
from astroattitude import NativeLibrary,ResourceError
from astroattitude.workflow import CalibrationSession
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('native',type=Path);p.add_argument('catalog',type=Path);p.add_argument('astrometry',type=Path);p.add_argument('journal',type=Path)
a=p.parse_args()
with CalibrationSession.restore(NativeLibrary(a.native),a.catalog.read_bytes(),a.astrometry.read_bytes(),a.journal.read_text()) as session:
    progress=session.progress()
    print(f'{progress.admitted_frames}/{progress.frames} frames admitted in {progress.blocks} blocks')
    print(session.next_action().reason)
    if progress.ready_to_fit:
        try:
            with session.fit() as candidate:
                print('Diagnostic candidate:',candidate.data.id)
                print('Plan fresh validation before considering export.')
        except ResourceError as error:
            print('More evidence needed:',error.error.detail)
            for model in session.model_diagnostics():print(model.family,model.status)
    print('Retrospective example. Conditional precision unestimated; absolute accuracy unqualified.')
