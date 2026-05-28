function processor_process(project, images, callback)
{
    console.log('PROJECT:');
    console.log(project);
    console.log('IMAGES:');
    console.log(images);

    console.log('HASH:');
    console.log(project.timeline[0].hash);

    const program_file_version = 2;         // Version should be incremented with every change of the output file

    // Command and parameter definitions ------------------------------------------------------------------------------------------------------------
    // Labels
    const command_label = 0xA1;
    const command_parameter_label = 0xB1;
    //const picture_label = 0xC1;
    const program_parameter_label = 0xD1;

    // Command types
    const picture_command = 1;
    const fixcol_command = 2;
    const inccol_command = 3;
    const repeat_command = 4;
    const end_command = 5;
    const keeplast_command = 6;

    // Command parameter types
    const color_type = 1;
    const speed_type = 2;
    const strobo_type = 3;
    const offset_type = 4;
    const startTime_type = 5;
    const endTime_type = 6;
    const frequency_type = 7;       // Line frequency
    const gap_type = 8;
    const dimming_type = 9;
    const width_type = 10;
    const height_type = 11;
    const picture_frequency_type = 12;
    const accelerometer_type = 13;
    const last_command_type = 14;

    // Program parameter types
    const headerSize_dw = 1;
    //const prgFileSize_dw = 2;     // Obsolette
    const prgName = 3;
    const numOfLeds = 4;
    const prgFileVersion = 5;
    const progEndBehavior = 6;

    // Program end behavior
    const exit_to_standby = 0;
    const repeat_program = 1;
    const keep_last_frame = 2;

    // Estimation of size of the program in Bytes and calculate first empty block where first picture can start -----------------------------------------
    const max_command_size_dw = 23;      // Maximum size of one command (including all possible parameters)
    const max_image_header_size_dw = 0;     // Image header was deleted (now it has 0 size)
    const block_size_B = 4096;
    var prg_size_B = project.timeline.length * (max_command_size_dw * 4);
    var picture_start_offset_B = (Math.ceil(prg_size_B / block_size_B)) * block_size_B;

    // Calculate size of all pictures and determine their start offset, store offsets to an array
    var images_offset_array = [];
    var i=0;
    for(var index in images){
        images_offset_array[index] = picture_start_offset_B;
        //images_offset_array.push([index,picture_start_offset_B]);
        var imgSize = (images[index].width * images[index].height * 4) + (max_image_header_size_dw  *4);
        picture_start_offset_B += Math.ceil(imgSize/block_size_B) * block_size_B;  
    }
    var sizeOfWholeProgramFile_B = picture_start_offset_B;

    // Create buffer for the complete program
    let buf = Buffer.allocUnsafe(sizeOfWholeProgramFile_B);
    buf.fill(0);

    
    
    // Fulfill the buffer with program data ------------------------------------------------------------------------------------------------------------
    var buf_pos_dw = 0;

    // Insert the program header and program parameters first
    insert_dw2buf( (program_parameter_label << 24) | (prgFileVersion << 16) | 1, buf, buf_pos_dw++ );    // Version of the program file
    insert_dw2buf(program_file_version, buf, buf_pos_dw++);

    insert_dw2buf( (program_parameter_label << 24) | (prgName << 16) | 8, buf, buf_pos_dw++ );          // program name                              
    buf.write(project.name, buf_pos_dw*4, 32);                                                          // Name (constrained to 32 Bytes)
    buf_pos_dw += 8;

    insert_dw2buf( (program_parameter_label << 24) | (numOfLeds << 16) | 1, buf, buf_pos_dw++ );        // Number of leds
    insert_dw2buf(project.leds, buf, buf_pos_dw++);                                                     // value

    insert_dw2buf( (program_parameter_label << 24) | (progEndBehavior << 16) | 1, buf, buf_pos_dw++ );        // Number of leds
    insert_dw2buf(project.post, buf, buf_pos_dw++); 

    insert_dw2buf( (program_parameter_label << 24) | (headerSize_dw << 16) | 1, buf, buf_pos_dw++ );    // Header size in DWs (must be here at the end of the header to show the right value)
    insert_dw2buf(buf_pos_dw+1, buf, buf_pos_dw++);


    // Load commands into arrays
    var last_command = false;
    var prog_array = [];
    for(var i=0; i<project.timeline.length; i++){
        var cmd = [];
        var j=1;

        if(i == project.timeline.length-1)
            last_command = true;

        // Start time
        cmd[j++] = (command_parameter_label << 24) | (startTime_type << 16) | 1;
        cmd[j++] = Math.max(0, project.timeline[i].start);
        // End time
        cmd[j++] = (command_parameter_label << 24) | (endTime_type << 16) | 1;
        cmd[j++] = project.timeline[i].end;
        // Dimming
        cmd[j++] = (command_parameter_label << 24) | (dimming_type << 16) | 1;
        cmd[j++] = project.timeline[i].dim;
        // Offset
        cmd[j++] = (command_parameter_label << 24) | (offset_type << 16) | 1;
        cmd[j++] = images_offset_array[project.timeline[i].hash];
        // Width of the image
        cmd[j++] = (command_parameter_label << 24) | (width_type << 16) | 1;
        cmd[j++] = images[project.timeline[i].hash].height;         // Transformed image height = width and vice versa
        // Height of the image
        cmd[j++] = (command_parameter_label << 24) | (height_type << 16) | 1;
        cmd[j++] = images[project.timeline[i].hash].width;
        // Frequency
        cmd[j++] = (command_parameter_label << 24) | (frequency_type << 16) | 1;
        cmd[j++] = Math.min(project.timeline[i].frequency, config.project.max_line_frequency || 2500);
        // Gap (space between pictures)
        cmd[j++] = (command_parameter_label << 24) | (gap_type << 16) | 1;
        cmd[j++] = project.timeline[i].gap;
        // Picture frequency
        cmd[j++] = (command_parameter_label << 24) | (picture_frequency_type << 16) | 1;
        cmd[j++] = project.timeline[i].picture_frequency;
        // Accelerometer enable
        cmd[j++] = (command_parameter_label << 24) | (accelerometer_type << 16) | 1;
        cmd[j++] = (project.timeline[i].accelerometer) ? 1 : 0;
        // Last command info (if this command is the last one in the program, the value is 1)
        cmd[j++] = (command_parameter_label << 24) | (last_command_type << 16) | 1;
        cmd[j++] = (i == project.timeline.length-1) ? 1 : 0;
        

        // Don't forget to change max_command_size_dw value when adding parameters here!!!


        // Strobo
        // If (strobo == true)
        //      load strobo parameters

        // Command identifier containing command size
        cmd[0] = (command_label << 24) | (picture_command << 16) | j;

        //prog_array[i] = cmd;     // Add this command array into the program array
        buf_pos_dw += insert_cmd2buf(cmd, buf, buf_pos_dw);
    }

    // Insert pictures
    for(var index in images_offset_array){
        var reordered_img = reorder_image_pixels_RGBA_to_DimBGR(images[index]);     // Reorder pixel data to APA102 format
        var rotated_img = rotate_image_data_array_right_90(images[index].width, images[index].height, reordered_img);   // Rotate image 90° right

        // Copy picture data to the buffer
        var buf_pos_dw = images_offset_array[index]/4;                      // Offset of the image data
        for(var i=0; i<rotated_img.length; i++){     
            insert_dw2buf(rotated_img[i], buf, buf_pos_dw++);
        }
    }
    

    callback(buf);
}


function insert_dw2buf(dw, buf, pos){
    buf.writeUInt32LE((new Uint32Array([dw]))[0], pos*4);
}

function insert_cmd2buf(cmd, buf, pos){
    var position = pos*4;
    var i=0;
    for(i; i<cmd.length; i++){  // Iterate through data of one command
        buf.writeUInt32LE((new Uint32Array([cmd[i]]))[0], position);
        position += 4;
    }
    return (i);           // Inserted DWs
}

function reorder_image_pixels_RGBA_to_DimBGR(image){
    var image_array32 = new Uint32Array(image.width*image.height);
    for(var i=0; i<image.data.length; i=i+4){       // Iterate through all pixels in image
        //image_array32[i/4] = ((0xE0 << 24) & 0xFF000000) | (image.data[i+1] << 16) | (image.data[i+2] << 8) | image.data[i];
        image_array32[i/4] = (image.data[i] << 24) | (image.data[i+1] << 16) | (image.data[i+2] << 8) | (0xE0 & 0xFF);
    }
    return image_array32;
}

function rotate_image_data_array_right_90(width, height, img_array32){
    var rotated_img_array = new Uint32Array(img_array32.length);
    var pos = 0;
    for(var j=0; j<width; j++){       // Move horizontally in the source image
        for(var i=height-1; i>=0; i--){  // Move vertically in the source image
            rotated_img_array[pos++] = img_array32[ (i*width) + j];
        }
    }
    return rotated_img_array;
}

function processor_process_aurax(project, images, callback)
{
    const axp_magic = 0x31505841; // "AXP1"
    const axp_version = 1;
    const axp_codec_raw = 0;
    const axp_codec_columns = 1;
    const axp_column_raw = 0;
    const axp_column_rle = 1;
    const axp_column_repeat = 2;

    var commands = [];
    var decodedOffset = 0;

    for (var i = 0; i < project.timeline.length; i++) {
        var node = project.timeline[i];
        var image = images[node.hash];
        var reordered = reorder_image_pixels_RGBA_to_DimBGR(image);
        var rotated = rotate_image_data_array_right_90(image.width, image.height, reordered);
        var raw = uint32ArrayToBuffer(rotated);
        var encoded = encodeAuraXColumns(raw, image.height, image.width);
        var rawCodec = { codec: axp_codec_raw, data: raw };
        var chosen = encoded.data.length < raw.length ? encoded : rawCodec;

        commands.push({
            startTime: Math.max(0, node.start),
            endTime: node.end,
            width: image.height,
            height: image.width,
            frequency: Math.min(node.frequency, config.project.max_line_frequency || 2500),
            decodedOffset: decodedOffset,
            codec: chosen.codec,
            data: chosen.data,
            isLast: (i == project.timeline.length - 1) ? 1 : 0,
        });
        decodedOffset += raw.length;
    }

    var commandWords = 10;
    var headerBytes = (6 * 4) + (commands.length * commandWords * 4);
    var dataOffset = headerBytes;
    var totalBytes = headerBytes;
    for (var c = 0; c < commands.length; c++) {
        commands[c].dataOffset = dataOffset;
        commands[c].dataSize = commands[c].data.length;
        dataOffset += commands[c].data.length;
        totalBytes += commands[c].data.length;
    }

    var buf = Buffer.allocUnsafe(totalBytes);
    buf.fill(0);
    var pos = 0;
    pos = writeDw(buf, pos, axp_magic);
    pos = writeDw(buf, pos, axp_version);
    pos = writeDw(buf, pos, commands.length);
    pos = writeDw(buf, pos, project.leds);
    pos = writeDw(buf, pos, project.post);
    pos = writeDw(buf, pos, decodedOffset);

    for (var c = 0; c < commands.length; c++) {
        var cmd = commands[c];
        pos = writeDw(buf, pos, cmd.startTime);
        pos = writeDw(buf, pos, cmd.endTime);
        pos = writeDw(buf, pos, cmd.width);
        pos = writeDw(buf, pos, cmd.height);
        pos = writeDw(buf, pos, cmd.frequency);
        pos = writeDw(buf, pos, cmd.dataOffset);
        pos = writeDw(buf, pos, cmd.dataSize);
        pos = writeDw(buf, pos, cmd.decodedOffset);
        pos = writeDw(buf, pos, cmd.codec);
        pos = writeDw(buf, pos, cmd.isLast);
        cmd.data.copy(buf, cmd.dataOffset);
    }

    callback(buf);

    function writeDw(buffer, offset, value) {
        buffer.writeUInt32LE((new Uint32Array([value]))[0], offset);
        return offset + 4;
    }

    function uint32ArrayToBuffer(arr) {
        var out = Buffer.allocUnsafe(arr.length * 4);
        for (var i = 0; i < arr.length; i++) {
            out.writeUInt32LE(arr[i], i * 4);
        }
        return out;
    }

    function encodeAuraXColumns(raw, width, height) {
        var colBytes = width * 4;
        var parts = [];
        var prev = null;

        for (var col = 0; col < height; col++) {
            var start = col * colBytes;
            var column = raw.slice(start, start + colBytes);

            if (prev && Buffer.compare(column, prev) === 0) {
                parts.push(Buffer.from([axp_column_repeat]));
                continue;
            }

            var rawColumn = Buffer.allocUnsafe(1 + colBytes);
            rawColumn[0] = axp_column_raw;
            column.copy(rawColumn, 1);

            var rleColumn = encodeRleColumn(column, width);
            parts.push(rleColumn.length < rawColumn.length ? rleColumn : rawColumn);
            prev = column;
        }

        return { codec: axp_codec_columns, data: Buffer.concat(parts) };
    }

    function encodeRleColumn(column, width) {
        var runs = [];
        var last = column.readUInt32LE(0);
        var count = 1;

        for (var i = 1; i < width; i++) {
            var pixel = column.readUInt32LE(i * 4);
            if (pixel === last && count < 65535) {
                count++;
            } else {
                runs.push({ count: count, pixel: last });
                last = pixel;
                count = 1;
            }
        }
        runs.push({ count: count, pixel: last });

        var out = Buffer.allocUnsafe(1 + 2 + runs.length * 6);
        out[0] = axp_column_rle;
        out.writeUInt16LE(runs.length, 1);
        var pos = 3;
        for (var r = 0; r < runs.length; r++) {
            out.writeUInt16LE(runs[r].count, pos);
            pos += 2;
            out.writeUInt32LE(runs[r].pixel, pos);
            pos += 4;
        }
        return out;
    }
}
