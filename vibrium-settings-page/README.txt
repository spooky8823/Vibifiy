Native settings page for Vibrium
================================
Easiest: from the repo root run
    git apply --ignore-whitespace settings-page.patch
(the patch also deletes Vibrium/c++/ui/console_window.cc and .h)

Or copy the files in this zip over the repo, then delete these two by hand:
    Vibrium/c++/ui/console_window.cc
    Vibrium/c++/ui/console_window.h
