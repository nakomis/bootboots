import { fireEvent, render, screen, within } from '@testing-library/react';
import { DEFAULT_CAMERA_SETTINGS } from '../device/types';
import { CameraSettingsPanel } from './CameraSettingsPanel';

test('commits typed values clamped to range, toggles and selects', async () => {
  const onChange = vi.fn();
  render(
    <CameraSettingsPanel settings={DEFAULT_CAMERA_SETTINGS} onSettingChange={onChange} disabled={false} />,
  );
  fireEvent.click(screen.getByText('Camera Settings'));

  const quality = screen.getByLabelText('JPEG Quality value');
  fireEvent.change(quality, { target: { value: '99' } });
  fireEvent.blur(quality);
  expect(onChange).toHaveBeenLastCalledWith('jpeg_quality', 63);
  fireEvent.change(quality, { target: { value: 'abc' } });
  fireEvent.keyDown(quality, { key: 'Enter' });
  fireEvent.blur(quality);
  expect(quality).toHaveValue('10');

  fireEvent.click(screen.getByRole('switch', { name: 'Vertical Flip' }));
  expect(onChange).toHaveBeenLastCalledWith('vflip', true);

  fireEvent.mouseDown(screen.getByRole('combobox', { name: 'WB Mode' }));
  fireEvent.click(within(await screen.findByRole('listbox')).getByText('Cloudy'));
  expect(onChange).toHaveBeenLastCalledWith('wb_mode', 2);
});
