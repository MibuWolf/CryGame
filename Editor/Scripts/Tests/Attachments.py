# Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

import unittest

class TestAttachmentFunctions(unittest.TestCase):
    def setUp(self):
        general.open_level_no_prompt("gamesdk/Levels/_TestMaps/Sandbox_Tests/Sandbox_Tests.cry")
        trackview.set_recording(False)

    def test_basic_attachments(self):
        self.assertEqual(general.get_object_parent("AnimObject2"), "")
        general.attach_object("AnimObject1", "AnimObject2", "", "")
        self.assertEqual(general.get_object_parent("AnimObject2"), "AnimObject1")
        self.assertEqual(general.get_position("AnimObject2"), (4, 0, 0))
        self.assertEqual(general.get_world_position("AnimObject2"), (1028, 1024, 0))
        general.set_position("AnimObject1", 1030, 1030, 0)
        self.assertEqual(general.get_world_position("AnimObject2"), (1034, 1030, 0))
        general.set_rotation("AnimObject1", 0, 0, -90)
        self.assertEqual(general.get_world_position("AnimObject2"), (1030, 1026, 0))
        general.set_scale("AnimObject1", 0.5, 0.5, 0.5)
        self.assertEqual(general.get_world_position("AnimObject2"), (1030, 1028, 0))
        self.assertEqual(general.get_position("AnimObject2"), (4, 0, 0))
        
    def test_undo_redo(self):
        general.attach_object("AnimObject1", "AnimObject2", "", "")        
        general.undo()
        self.assertEqual(general.get_object_parent("AnimObject2"), "")
        self.assertEqual(general.get_position("AnimObject2"), (1028, 1024, 0))
        general.redo()
        self.assertEqual(general.get_object_parent("AnimObject2"), "AnimObject1")
        self.assertEqual(general.get_position("AnimObject2"), (4, 0, 0))
        general.undo()
        self.assertEqual(general.get_object_parent("AnimObject2"), "")
        self.assertEqual(general.get_position("AnimObject2"), (1028, 1024, 0))
        general.attach_object("AnimObject1", "AnimObject2", "", "")
        self.assertEqual(general.get_object_parent("AnimObject2"), "AnimObject1")
        general.detach_object("AnimObject2")
        self.assertEqual(general.get_object_parent("AnimObject2"), "")
        general.undo()
        self.assertEqual(general.get_object_parent("AnimObject2"), "AnimObject1")
        self.assertEqual(general.get_position("AnimObject2"), (4, 0, 0))
        general.redo()
        self.assertEqual(general.get_object_parent("AnimObject2"), "")
        self.assertEqual(general.get_position("AnimObject2"), (1028, 1024, 0))
        
unittest.main(exit = False, verbosity = 2, defaultTest="TestAttachmentFunctions")