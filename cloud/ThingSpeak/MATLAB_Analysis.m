% BioSync cloud-side analysis for a private ThingSpeak channel.
% Set these two values in the ThingSpeak MATLAB Analysis app before running.
channelID = 123456;
readAPIKey = 'PASTE_CHANNEL_READ_API_KEY_HERE';
numPoints = 500;

data = thingSpeakRead(channelID, ...
    'ReadKey', readAPIKey, ...
    'NumPoints', numPoints, ...
    'Fields', 1:8);

if isempty(data)
    disp('No channel samples are available yet.');
    return;
end

% Channel fields: left, middle, right FSR; temp; humidity;
% posture code; alert code; cumulative motor-on seconds.
posture = data(:, 6);
alert = data(:, 7);
active = isfinite(posture) & posture > 0;
classified = posture(active);

if isempty(classified)
    disp('No pressure-present samples are in the selected cloud window.');
else
    codes = [1 2 3 4];
    labels = {'LEFT_DOMINANT', 'CENTER_DOMINANT', ...
              'RIGHT_DOMINANT', 'DISTRIBUTED'};
    counts = arrayfun(@(code) sum(classified == code), codes);
    shares = 100 * counts / numel(classified);
    postureChanges = sum(diff(classified) ~= 0);
    fprintf('Pressure-present cloud samples: %d\n', numel(classified));
    fprintf('Observed posture-category changes: %d\n', postureChanges);
    for k = 1:numel(codes)
        fprintf('%s: %.1f%% (%d samples)\n', labels{k}, shares(k), counts(k));
    end
end

temperature = data(:, 4);
humidity = data(:, 5);
fprintf('Mean local temperature: %.1f C\n', mean(temperature, 'omitnan'));
fprintf('Mean local relative humidity: %.1f%%\n', mean(humidity, 'omitnan'));

alert = alert(isfinite(alert));
if numel(alert) > 1
    alertEpisodes = sum(alert(2:end) == 1 & alert(1:end-1) ~= 1);
else
    alertEpisodes = 0;
end
fprintf('Alert starts observed in selected window: %d\n', alertEpisodes);

motorSeconds = data(:, 8);
motorSeconds = motorSeconds(isfinite(motorSeconds));
if ~isempty(motorSeconds)
    fprintf('Latest session motor-on time counter: %.1f seconds\n', motorSeconds(end));
end
