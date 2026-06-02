function IgnisSlider(el)
{
    this.el = $(el);
    this.min = this.toFloat(this.el.attr('min'));
    this.max = this.toFloat(this.el.attr('max'));
    this.step = this.toFloat(this.el.attr('step'));
    this.val = this.toFloat(this.el.val());

    if (this.step == 0) this.step = 1;

    this.hmd = false;

    this.el.data('slider', this);
    //this.el.hide();
    this.slider_el = $('<div class="ignis-slider"></div>');
    this.el.after(this.slider_el);

    this.slider_el_minus = $('<div class="slider-thumb minus">-</div>');
    this.slider_el_plus = $('<div class="slider-thumb plus">+</div>');
    this.slider_el_handle = $('<div class="slider-handle"></div>');
    this.slider_el_progress = $('<div class="slider-progress"></div>');
    this.slider_el_bar = $('<div class="slider-bar"></div>');

    this.slider_el.append(this.slider_el_minus);
    this.slider_el.append(this.slider_el_plus);
    this.slider_el.append(this.slider_el_bar);
    this.slider_el_bar.append(this.slider_el_handle);
    this.slider_el_bar.append(this.slider_el_progress);

    this.slider_el_plus.on('click', $.proxy(this.inc, this));
    this.slider_el_minus.on('click', $.proxy(this.dec, this));
    this.slider_el_bar.on('click', $.proxy(this.barClicked, this));
    this.slider_el_handle.on('mousedown', $.proxy(this.handleMdown, this));
    this.slider_el_bar.on('mousemove', $.proxy(this.barMouseMove, this));
    $(document).on('mousemove', $.proxy(this.documentMouseMove, this));
    $(document).on('mouseup', $.proxy(this.handleMup, this));

    this.el.on('change', $.proxy(this.elValUpdate, this));

    setInterval($.proxy(this.valCheck, this), 100);
    this.valUpdate();

    $(window).on('load resize', $.proxy(this.valUpdate, this));
    setTimeout($.proxy(this.valUpdate, this), 1000);
    setInterval($.proxy(this.valUpdateEx, this), 1000);
    setInterval($.proxy(this.updateAttributes, this), 100);
}

IgnisSlider.prototype.updateAttributes = function ()
{
    var max = this.toFloat(this.el.attr('max'));
    if (this.max != max) {
        this.max = max;
        this.update();
    }
}

IgnisSlider.prototype.barMouseMove = function (e)
{
    if (!this.hmd) return;
    this.updateFromPageX(e.pageX);
}

IgnisSlider.prototype.documentMouseMove = function (e)
{
    if (!this.hmd) return;
    this.updateFromPageX(e.pageX);
}

IgnisSlider.prototype.updateFromPageX = function (pageX)
{
    var x = pageX - this.slider_el_bar.offset().left;
    this.val = this.posToVal(x);
    this.update();
}

IgnisSlider.prototype.handleMdown = function (e)
{
    this.hmd = true;
    e.preventDefault();
}

IgnisSlider.prototype.handleMup = function (e)
{
    this.hmd = false;
}

IgnisSlider.prototype.barClicked = function (e)
{
    if (e.target != this.slider_el_bar[0] && e.target != this.slider_el_progress[0]) return;
    
    this.val = this.posToVal(e.offsetX);
    
    this.update();
}

IgnisSlider.prototype.posToVal = function (x)
{
    var w = this.slider_el_bar.width();
    var hw = this.slider_el_handle.width();
    var m = w - hw;
    x = x - (hw / 2);

    var range = this.max - this.min;
    var val = this.clamp(this.min + ((range / m) * x), this.min, this.max);
    var val = Math.round(val / this.step) * this.step;
    return val;
}

IgnisSlider.prototype.inc = function ()
{
    this.val += this.step;
    if (this.val > this.max) this.val = this.max;
    this.update();
}

IgnisSlider.prototype.dec = function ()
{
    this.val -= this.step;
    if (this.val < this.min) this.val = this.min;
    this.update();
}

IgnisSlider.prototype.update = function ()
{
    this.valUpdate();

    var old_val = this.toFloat(this.el.val());

    if (old_val != this.val) {
        this.el.val(this.val);
        this.el.trigger('input');
        this.el.trigger('change');
    }
}

IgnisSlider.prototype.valCheck = function ()
{
    var val = this.toFloat(this.el.val());
    if (val != this.val) {
        this.elValUpdate();
    }
}

IgnisSlider.prototype.elValUpdate = function ()
{
    var ov = this.val = this.toFloat(this.el.val());
    if (this.val < this.min) this.val = this.min;
    if (this.val > this.max) this.val = this.max;
    if (ov != this.val) {
        this.el.val(this.val);
        this.el.trigger('change');
        return;
    }

    this.valUpdate();
}

IgnisSlider.prototype.valUpdateEx = function ()
{
    if (!this.hmd) this.valUpdate();
}

IgnisSlider.prototype.valUpdate = function ()
{
    var p_max = this.slider_el_bar.width() - this.slider_el_handle.width();

    var range = this.max - this.min;
    if (range == 0) range = 1;

    var p = Math.round((p_max / range) * (this.val - this.min));

    this.slider_el_handle.css('left', p + 'px');
    this.slider_el_progress.css('width', p + 'px');
}

IgnisSlider.prototype.clamp = function (val, min, max)
{
    if (val < min) val = min;
    if (val > max) val = max;
    return val;
}

IgnisSlider.prototype.toFloat = function (value)
{
    var i = parseFloat(value);
    i = (isNaN(i) ? 0 : i);
    return i;
}

function IgnisSliderInit()
{
    $('input[type=slider]').each(function () {
        new IgnisSlider(this);
    });
}
