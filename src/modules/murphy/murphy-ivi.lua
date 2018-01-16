zone { name = "driver" }
zone { name = "passenger1" }
zone { name = "passenger2" }
zone { name = "passenger3" }
zone { name = "passenger4" }

routing_group {
    name = "default_driver",
    node_type = node.output,
    accept = function(self, n)
        return (n.type ~= node.bluetooth_carkit and n.type ~= node.hdmi)
    end,
    compare = builtin.method.compare_default
}

routing_group {
    name = "default_passenger1",
    node_type = node.output,
    accept = function(self, n)
        return (n.type == node.hdmi or n.name == 'Silent')
    end,
    compare = builtin.method.compare_default
}

routing_group {
    name = "phone",
    node_type = node.input,
    accept = builtin.method.accept_phone,
    compare = builtin.method.compare_phone
}

routing_group {
    name = "phone",
    node_type = node.output,
    accept = builtin.method.accept_phone,
    compare = builtin.method.compare_phone
}

application_class {
    class = "event",
    node_type = node.event,
    priority = 6,
    route = {
        output = { driver = routing_group.default_driver_output }
    },
    roles = { event  = no_resource }
}

application_class {
    class = "phone",
    node_type = node.phone,
    priority = 5,
    route = {
        input  = { driver = routing_group.phone_input },
        output = {driver = routing_group.phone_output }
    },
    roles = { phone = no_resource, carkit = no_resource }
}

application_class {
    node_type = node.alert,
    priority = 4,
    route = {
        output = { driver = routing_group.default_driver_output },
    },
    roles = { ringtone = no_resource, alarm = no_resource }
}

application_class {
    class = "navigator",
    node_type = node.navigator,
    priority = 3,
    route = {
        output = { driver = routing_group.default_driver_output,
               passenger1 = routing_group.default_passenger1_output }
    },
    roles = { navigator = {0, "autorelease", "mandatory", "shared"}, speech = no_resource }
}

application_class {
    class = "game",
    node_type = node.game,
    priority = 2,
    route = {
        output = { driver = routing_group.default_driver_output,
               passenger1 = routing_group.default_passenger1_output }
    },
    roles = { game = {0, "mandatory", "exclusive"} }
}

application_class {
    class = "player",
    node_type = node.radio,
    priority = 1,
    route = {
        output = { driver = routing_group.default_driver_output }
    },
    roles = { radio = {1, "mandatory", "exclusive"} }
}

application_class {
    class = "player",
    node_type = node.player,
    priority = 1,
    route = {
        output = { driver = routing_group.default_driver_output,
                   passenger1 = routing_group.default_passenger1_output }
    },
    roles = { music    = {0, "mandatory", "exclusive"},
              video    = {0, "mandatory", "exclusive"},
              test     = {0, "mandatory", "exclusive"},
              bt_music = no_resource,
              player   = no_resource
    }
}

application_class {
    class = "player",
    node_type = node.browser,
    priority = 1,
    route = {
        output = { driver = routing_group.default_driver_output,
               passenger1 = routing_group.default_passenger1_output }
    },
    roles = { browser = {0, "mandatory", "shared"} }
}

audio_resource {
    name = { recording = "audio_recording", playback = "audio_playback" },
    attributes = {
       role = {"media.role", mdb.string, "music"},
       pid  = {"application.process.id", mdb.string, "<unknown>"},
       name = {"resource.set.name", mdb.string, "<unknown>"},
       appid = {"resource.set.appid", mdb.string, "<unknown>"}
    }
}

mdb.import {
    table = "speedvol",
    columns = {"value"},
    condition = "zone = 'driver' AND device = 'speaker'",
    maxrow = 1,
    update = builtin.method.make_volumes
}

mdb.import {
    table = "audio_playback_owner",
    columns = {"zone_id", "application_class", "role"},
    condition = "zone_name = 'driver'",
    maxrow = 1,
    update = function(self)
        zid = self[1].zone_id
    if (zid == nil) then zid = "<nil>" end
    class = self[1].application_class
    if (class == nil) then class = "<nil>" end
    role = self[1].role
    if (role == nil) then role = "<nil>" end
--      print("*** import "..self.table.." update: zone:"..zid.." class:"..class.." role:"..role)
    end
}

volume_limit {
    name = "phone_suppress",
    type = volume_limit.class,
    limit = -20,
    node_type = { node.phone },
    calculate = builtin.method.volume_supress
}

volume_limit {
    name = "navi_suppress",
    type = volume_limit.class,
    limit = -20,
    node_type = { node.navigator, node.phone },
    calculate = builtin.method.volume_supress
}

volume_limit {
    name = "navi_maxlim",
    type = volume_limit.maximum,
    limit = -10,
    node_type = { node.navigator }
}

volume_limit {
    name = "player_maxlim",
    type = volume_limit.maximum,
    limit = -20,
    node_type = { node.player }
}
