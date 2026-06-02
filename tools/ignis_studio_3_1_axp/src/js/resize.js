var resize_queue = 0;

function resize(from, to, w, h, callback, force, idx)
{
    if (fs.existsSync(to)) {
        if (callback) callback(true, idx);
        return;
    }

    resize_queue++;

    const gm = require('gm');

    gm(from).resize(w,h,(force ? '!' : '')).write(to, function (err) {
        if (err != undefined) {
            resize_jimp(from, to, w, h, callback, force);
        } else {
            resize_queue--;
            if (callback) callback(true, idx);
        }
    });
}

function resize_jimp(from, to, w, h, callback, force)
{
    var Jimp = require('jimp');

    Jimp.read(from)
        .then(img => {
            if (force) {
                img.resize(w, h); // resize
            } else {
                img.scaleToFit(w, h); // scale
            }
            
            img.quality(80) // set JPEG quality
                .write(to); // save
            resize_queue--;
            if (callback) callback(true);
            return img;
        })
        .catch(err => {
            resize_queue--;
            if (callback) callback(false);
        });
}