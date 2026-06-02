function IgnisUserconf(ignis)
{
    this.ignis = ignis;
    ignis.userconf = this;
    this.defaults = {
        custom_leds: 100,
        led_definitions: config.project.pixel_counts,
        last_leds: config.project.default_leds,
        last_selected_device: '',
        last_enable_accelerometer: true,
        last_line_frequency: config.project.node_frequency
    };
    this.data = Object.assign({}, this.defaults);
}

IgnisUserconf.prototype.init = function ()
{
    if (!this.load()) {
        this.save();
    }
}

IgnisUserconf.prototype.get = function (key)
{
    return this.data[key];
}

IgnisUserconf.prototype.set = function (key, value)
{
    this.data[key] = value;
    this.save();
}

IgnisUserconf.prototype.load = function ()
{
    var cfgfile = ignis_appdir() + path.sep + 'user.conf';

    if (!fs.existsSync(cfgfile)) return false;

    var buffer = fs.readFileSync(cfgfile, 'UTF-8');
    this.data = Object.assign({}, this.defaults, JSON.parse(buffer));

    return true;
}

IgnisUserconf.prototype.save = function ()
{
    var cfgfile = ignis_appdir() + path.sep + 'user.conf';

    fs.writeFileSync(cfgfile, JSON.stringify(this.data), 'UTF-8');
}
