<?php

/**
 * book add pages form
 * @param array $form_state
 * @param string $pid
 * @param string $page_cmodel
 * @param string $relation
 * @return array
 */
function book_add_pages_form(&$form_state, $pid, $page_cmodel = 'islandora:pageCModel', $relation = 'isMemberOf') {
  $file = (isset($form_state['values']['ingest-file-location']) ? $form_state['values']['ingest-file-location'] : '');
  $form['#attributes']['enctype'] = 'multipart/form-data';
  $form['pid'] = array(
    '#type' => 'hidden',
    '#value' => $pid,
  );
  $form['page_cmodel'] = array(
    '#type' => 'hidden',
    '#value' => $page_cmodel,
  );
  $form['relation'] = array(
    '#type' => 'hidden',
    '#value' => $relation,
  );

  $form['#attributes'] = array('enctype' => 'multipart/form-data');
  $form['upload_options'] = array(
    '#type' => 'fieldset',
    '#title' => "<strong>Access zipped page images</strong>",
    '#description' => t('Select either ftp file location on the server, or upload zipped page images, but not both.'),
  );

  $form['upload_options']['file_upload'] = array(
    '#type' => 'file',
    '#title' => t('Upload Zipped TIFFs'),
    '#size' => 48,
    '#description' => t('Upload a zip file containing a tiff for each page of this book'),
  );
  $form['upload_options']['upload_from_server'] = array(
    '#type' => 'textfield',
    '#description' => 'Server location of previously uploaded file',
    '#title' => 'FTP file location',
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#weight' => 10,
    '#value' => 'Ingest Pages'
  );
  return $form;
}

/**
 * book add pages form validate
 * @param array $form
 * @param array $form_state
 */
function book_add_pages_form_validate($form, &$form_state) {

  $fieldName = 'file_upload';
  if (isset($_FILES['files']) && is_uploaded_file($_FILES['files']['tmp_name'][$fieldName])) {
    $file = file_save_upload($fieldName);
    if ($file->filemime != 'application/zip') {
      form_set_error($fieldName, 'Input file must be a .zip file');
      return;
    }
    if (!$file) {
      form_set_error($fieldName, 'Error uploading file.');
      return;
    }
    $form_state['values']['file'] = $file->filepath;
  }

  if ($form_state['values']['upload_from_server']) {
    if (is_file($form_state['values']['upload_from_server'])) {
      $form_state['values']['file'] = $form_state['values']['upload_from_server'];
    }
    else {
      form_set_error('upload_from_server', 'File not found.');
      return;
    }
  }

  if (!isset($form_state['values']['file'])) {
    form_set_error($fieldName, 'Exactly one of these fields must be populated.');
    return;
  }
}

/**
 * book add pages form submit
 * @param array $form
 * @param array $form_state
 */
function book_add_pages_form_submit($form, &$form_state) {
  $pid = $form_state['values']['pid'];
  $dirName = str_replace(':', '_', $pid);
  $current_files = scandir(file_directory_path());
  foreach ($current_files as $current_file) {
    if ($current_file != '.' && $current_file != '..' && is_dir($current_file)) {
      $fileage = time() - filectime(file_directory_path() . "/$current_file");
      if ($fileage > 60 * 60) {
        deleteDirectory(file_directory_path() . "/$current_file");
      }
    }
  }
  $tmpDir = file_directory_path() . '/' . $dirName . '/';
  if (!is_dir($tmpDir)) {
    mkdir($tmpDir, 0766);
  }
  $zip_file = $form_state['values']['file'];

  // $zip_file = $file->filepath;
  $file_list = array();
  $cmdString = "unzip -q -o -d $tmpDir \"$zip_file\"";

  system($cmdString, $retVal);
  file_delete("$tmpDir \"$zip_file\"");

  $dirs = array();
  array_push($dirs, $tmpDir);
  $files = scandir($tmpDir);

  foreach ($files as $file) {
    if (is_dir("$tmpDir/$file") && $file != '.' && $file != '..' && $file != '__MACOSX') {
      array_push($dirs, $tmpDir . $file);
    }
  }

  foreach ($dirs as $directory) {
    if ($tiff_dir = opendir($directory)) {
      while (FALSE !== ($file_name = readdir($tiff_dir))) {
        $fullfile = $directory . $file_name;
        if (!is_dir($fullfile)) {
          $new_file_name = preg_replace("/[^a-z\.\d]/i", "", $file_name);
          $new_file_name = preg_replace("/\.tiff$/", ".tif", $new_file_name);
          if ($new_file_name != $file_name) {
            rename($directory . "/" . $file_name, $directory . "/$new_file_name");
            $file_name = $new_file_name;
          }
        }
        $ext = strrchr($file_name, '.');
        if ($ext == '.tif' || $ext == '.tiff') {
          array_push($file_list, "$directory" . "/$file_name");
        }
      }
      closedir($tiff_dir);
    }
  }
  sort($file_list);
  ibook_create_page_objects($pid, $file_list, $form_state['values']['page_cmodel'], $form_state['values']['relation'], $tmpDir);
}

/**
 * Create a page object for each element in $image_list as a child object of $pid
 * @param string $pid
 * @param array $image_list
 * @param string $page_cmodel
 * @param string $relation
 * @param string $tmpDir
 * @return boolean
 */
function ibook_create_page_objects($pid, $image_list = array(), $page_cmodel = 'islandora:pageCModel', $relation = 'isMemberOfCollection', $tmpDir) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'fedora_repository', 'api/ObjectHelper');
  $book = new Fedora_Item($pid);
  if (!$book->exists()) {
    return FALSE;
  }
  $dirName = str_replace(':', '_', $pid);
  $page_pids = array();
  $image_count = count($image_list);
  $batch = array(
    'title' => "Creating $image_count page objects",
    'operations' => array(),
    'file' => drupal_get_path('module', 'islandora_book') . '/add_pages.inc',
  );
  $page_num = 1;
  $book_title = $book->objectProfile->objLabel;
  foreach ($image_list as $image_path) {
    $batch['operations'][] = array('ibook_add_single_page_object', array($pid, $image_path, $page_num, "$book_title - Page " . sprintf("%03d", $page_num), $page_cmodel, $relation));
    $page_num++;
  }
  $batch['operations'][] = array('makePDF', array($pid, $image_list, $tmpDir));
  $batch['operations'][] = array('deleteDirectory', array($tmpDir));
  batch_set($batch);
  $objectHelper = new ObjectHelper;
  $dc_xml = $objectHelper->getStream($pid, 'DC');
  if (!preg_match('/<dc:type>ingested<\/dc:type>/', $dc_xml)) {
    $xml = new DOMDocument();
    $xml->loadXML($dc_xml);
    $root = $xml->documentElement;
    $typeNode = $xml->createElement('dc:type', 'ingested');
    $typeNode = $xml->createElementNS("http://purl.org/dc/elements/1.1/", 'dc:type', 'ingested');
    $root->appendChild($typeNode);
    $book->modify_datastream_by_value($xml->saveXML(), 'DC', 'Dublin core', 'text/xml', TRUE, "Added Type of Ingested", FALSE);
  }
}

/**
 * ibook add single page object
 * @param string $book_pid
 * @param string $page_file
 * @param int $page_num
 * @param string $page_title
 * @param string $page_cmodel
 * @param string $relation
 */
function ibook_add_single_page_object($book_pid, $page_file, $page_num = 1, $page_title = NULL, $page_cmodel = 'islandora:pageCModel', $relation = 'isMemberOf') {
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  $page_item = Fedora_Item::ingest_new_item("$book_pid-" . sprintf("%03d", $page_num), 'A', $page_title);
  $page_item->add_relationship('hasModel', $page_cmodel, FEDORA_MODEL_URI);
  $page_item->add_relationship($relation, $book_pid);
  // add original tiff
  $page_item->add_datastream_from_file($page_file, 'TIFF', 'Archival TIFF', 'image/tiff', 'M');
  $page_file = preg_replace('/\/\//', '/', $page_file);

  //add compresed jp2 for viewer
  if (variable_get('islandora_book_create_images', TRUE)) {
    exec('kdu_compress -i "' . $page_file . '" -o "' . $page_file . '.jp2' . '" -rate 0.5 Clayers=1 Clevels=7 "Cprecincts={256,256},{256,256},{256,256},{128,128},{128,128},{64,64},{64,64},{32,32},{16,16}" "Corder=RPCL" "ORGgen_plt=yes" "ORGtparts=R" "Cblk={32,32}" Cuse_sop=yes', $output, $returnValue);
    if ($returnValue == '0') {
      $page_item->add_datastream_from_file("$page_file.jp2", 'JP2', "Compressed JP2", 'image/jp2', 'M');
    }

    //add thumbnail image
    exec("convert {$page_file}[0] -colorspace RGB -thumbnail 120x $page_file-tn.jpg", $output, $returnValue);
    if ($returnValue == '0') {
      $page_item->add_datastream_from_file("$page_file-tn.jpg", 'TN', 'Thumbnail', 'image/jpeg', 'M');
      if ($page_num == 1) {
        $book_item = new Fedora_Item($book_pid);
        $DSIDS = $book_item->get_datastreams_list_as_array();
        if (array_key_exists("TN", $DSIDS)) {
          $book_item->purge_datastream('TN');
        }
        $book_item->add_datastream_from_file("$page_file-tn.jpg", 'TN', 'Thumbnail', 'image/jpeg', 'M');
      }
    }
    //add jpeg
    exec("convert  -resize 800 \"{$page_file}\"[0]  \"$page_file-med.jpg\"", $output, $returnValue);
    if ($returnValue == '0') {
      $page_item->add_datastream_from_file("$page_file-med.jpg", 'JPEG', "JPEG image", 'image/jpeg', 'M');
    }
  }
  //perform OCR using Tesseract, add text datastream

  if (variable_get('islandora_book_do_ocr', TRUE)) {
    $executable = variable_get('islandora_ocr_path', '/usr/bin/tesseract');
    $returnValue = "";
    $output = "";
    exec("$executable $page_file $page_file -l eng", $output, $returnValue);
    if ($returnValue == '0') {
      $page_item->add_datastream_from_file("$page_file.txt", 'OCR', "Scanned Text", 'text/plain', 'M');
    }
  }
}

/**
 * delete directory
 * @param string $dir
 * @return boolean
 */
function deleteDirectory($dir) {
  if (!file_exists($dir))
    return TRUE;
  if (!is_dir($dir))
    return unlink($dir);
  foreach (scandir($dir) as $item) {
    if ($item == '.' || $item == '..')
      continue;
    if (!deleteDirectory($dir . DIRECTORY_SEPARATOR . $item))
      return FALSE;
  }
  return rmdir($dir);
}

/**
 * make PDF
 * @param string $book_pid
 * @param array $file_list
 * @param string $tmpDir
 */
function makePDF($book_pid, $file_list, $tmpDir) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  $book_item = new Fedora_Item($book_pid);
  $outputFile = "${tmpDir}book.pdf";
  $tiffs = array();
  $cmdString = 'convert -compress LZW';
  foreach ($file_list as $tiff) {
    $cmdString .= " $tiff";
  }
  $cmdString .= " $outputFile";
  exec($cmdString, $output, $returnValue);
  $datastreams = $book_item->get_datastreams_list_as_array();
  if ($returnValue == '0') {
    if (array_key_exists('PDF', $datastreams)) {
      $book_item->purge_datastream('PDF');
    }
    $book_item->add_datastream_from_file($outputFile, 'PDF', "PDF of Book", 'application/pdf', 'M');
  }
}

function govdocs_management_form(&$form_state, $this_collection_pid) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  module_load_include('module', 'fedora_repository', 'fedora_repository');
  $restricted = FALSE;
  if (variable_get('fedora_namespace_restriction_enforced', TRUE)) {
    $restricted = TRUE;
    $allowed_string = variable_get('fedora_pids_allowed', 'default: demo: changeme: islandora:');
    $namespaces = explode(':', $allowed_string);
    foreach ($namespaces as $namespace) {
      if ($namespace) {
        $allowed[trim($namespace)] = trim($namespace);
      }
    }
  }
  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  module_load_include('inc', 'fedora_repository', 'CollectionPolicy');
  $item = new Fedora_Item($this_collection_pid);
  $ds_array = $item->get_datastreams_list_as_array();
  $collection_name = $item->objectProfile->objLabel;
  $form_names = islandora_content_model_get_form_names('islandora:govdocscollectionCModel');

  $content_models = get_content_models_as_option_array();
  $form['#attributes'] = array('enctype' => "multipart/form-data");
  $form['child_creation'] = array(
    '#title' => "Create Subsection",
    '#type' => 'fieldset',
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
  );

  $form['child_creation']['titlebox'] = array(
    '#type' => 'item',
    '#value' => t("Create New Subsection within $this_collection_pid"),
  );

  $form['child_creation']['collection_name'] = array(
    '#title' => "Collection Name",
    '#type' => 'textfield',
    '#description' => t("Human readable name for this subsection"),
  );

  $form['parent_collection'] = array(
    '#type' => 'hidden',
    '#value' => $this_collection_pid,
  );

  $form['collection_pid'] = array(
    '#type' => 'hidden',
    '#value' => $this_collection_pid,
  );

  $form['child_creation']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Create Subsection'),
    '#id' => 'create_class'
  );

  if (array_key_exists('MODS', $ds_array)) {
    $form['mods_creation'] = array(
      '#title' => "Create MODS record",
      '#description' => t('MODS record already exists! Please edit the existing form rather than uploading a new one.'),
      '#type' => 'fieldset',
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
    );
  }
  else {
    $form['mods_creation'] = array(
      '#title' => "Create MODS record",
      '#type' => 'fieldset',
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
    );

    $form['mods_creation']['upload'] = array(
      '#type' => 'file',
      '#title' => t('Upload MODS XML or Refworks file'),
      '#size' => 50,
    );

    $form['mods_creation']['url'] = array(
      '#type' => 'textfield',
      '#title' => t('MODS URL'),
      '#size' => 40,
    );

    $form['mods_creation']['submit'] = array(
      '#type' => 'submit',
      '#value' => t('Submit'),
      '#id' => 'mods_submit',
    );
  }

  if (user_access('delete entire collections')) {
    $form['delete_collection'] = array(
      '#title' => "Delete children from $collection_name",
      '#type' => 'fieldset',
      '#description' => t("Clicking this button will delete all child collections within $collection_name. <br /> <strong>This action cannot be undone.</strong>"),
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
    );
    $form['delete_collection']['confirm'] = array(
      '#title' => "Are you sure?",
      '#type' => 'fieldset',
      '#description' => t('<strong>Clicking the delete button will permanantly remove all child collections from this collection. <br />  <strong>This action cannot be undone.</strong> '),
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
    );

    $form['delete_collection']['confirm']['submit'] = array(
      '#type' => 'submit',
      '#value' => t('Delete this collection'),
      '#id' => 'delete_collection',
    );
  }
  return($form);
}

/**
 * collection creation form validate 
 * @param array $form
 * @param array $form_state
 */
function govdocs_management_form_validate($form, &$form_state) {
  if ($form_state['clicked_button']['#id'] == 'mods_submit' && file_save_upload('upload') == 0 && $form_state['values']['url'] == NULL) {
    form_set_error('url', t('You must upload a file or enter a URL!'));
  }

  if ($form_state['clicked_button']['#id'] == 'create_class' && $form_state['values']['collection_name'] == NULL) {
    form_set_error('collection_name', t('You must enter a name for the new subsection!'));
  }
}

/**
 * collection creation form submit
 * @global type $user
 * @param type $form
 * @param type $form_state
 */
function govdocs_management_form_submit($form, &$form_state) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_collection');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'fedora_repository', 'api/dublin_core');
  module_load_include('inc', 'fedora_repository', 'CollectionManagement');
  global $user;
  global $base_url;
  $path = drupal_get_path('module', 'islandora_govdocs');
  $collection_pid = $form_state['values']['parent_collection'];
  $policy = CollectionPolicy::loadFromCollection($collection_pid, TRUE);

  if ($form_state['clicked_button']['#id'] == 'create_class') {
    $module_path = drupal_get_path('module', 'fedora_repository');
    $thumbnail = drupal_get_path('module', 'Fedora_Repository') . '/images/Crystal_Clear_filesystem_folder_grey.png';
    $collection_view = drupal_get_path('module', 'islandora_govdocs') . '/xsl/govdocs.xsl';
    $pid_namespace = 'islandora';
    $new_collection_pid = Fedora_Item::get_next_PID_in_namespace($pid_namespace);
    $new_collection_label = $form_state['values']['collection_name'];
    $all_cModels = get_content_models_as_option_array();
    $collection_policy = '<?xml version="1.0" encoding="UTF-8"?>
<collection_policy xmlns="http://www.islandora.ca" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" name="" xsi:schemaLocation="http://www.islandora.ca http://syn.lib.umanitoba.ca/collection_policy.xsd">
    <content_models>
    </content_models>
    <search_terms>
    </search_terms>
    <staging_area></staging_area>
    <relationship>isMemberOfCollection</relationship>
</collection_policy>';
    $content_models = array('islandora:govdocsbookCModel');
    $collection_policy_xml = simplexml_load_string($collection_policy);
    foreach ($content_models as $content_model) {
      if ($content_model) {
        $node = $collection_policy_xml->content_models->addChild('content_model');
        $node->addAttribute('dsid', 'ISLANDORACM');
        $node->addAttribute('name', $all_cModels[$content_model]);
        $node->addAttribute('namespace', $pid_namespace . ':1');
        $node->addAttribute('pid', $content_model);
      }
    }
    $item = fedora_item::ingest_new_item($new_collection_pid, 'A', $new_collection_label, $user->name);
    $item->add_relationship('isMemberOfCollection', $collection_pid, RELS_EXT_URI);
    $item->add_relationship('hasModel', 'islandora:govdocscollectionCModel', FEDORA_MODEL_URI);
    $item->add_datastream_from_string($collection_policy_xml->saveXML(), 'COLLECTION_POLICY');
    $item->add_datastream_from_file($thumbnail, 'TN');
    $item->add_datastream_from_file($collection_view, 'COLLECTION_VIEW');

    $collection = new Fedora_Item($collection_pid);
    $collection_mods = $collection->get_datastream_dissemination('MODS');
    if ($collection_mods != NULL) {
      $mods_xml = new DOMDocument();
      $mods_xml->loadXML($collection_mods);
      $xsl_dom = new DOMDocument();
      $xsl_dom->load($path . '/xsl/related-item.xsl');
      $mods_xsl = new XSLTProcessor();
      $mods_xsl->importStylesheet($xsl_dom);
      $mods_string = $mods_xsl->transformToXml($mods_xml);
      drupal_set_message('XML before: ' . $mods_string);
      $mods_string = preg_replace('/<mods(.*?)>/', '', $mods_string);
      $mods_string = '<?xml version="1.0"?><mods xmlns="http://www.loc.gov/mods/v3">' . str_replace(array('<?xml version="1.0"?>', '</mods>'), '', $mods_string) . '</mods>';
      drupal_set_message('XML: ' . $mods_string);
      $item->add_datastream_from_string($mods_string, 'MODS');
    }
    $item->add_relationship('isPartOf', $collection_pid);
    $collection_item = new Fedora_Item($collection_pid);
    $collection_item->add_relationship('hasPart', $new_collection_pid);
    drupal_goto("fedora/repository/$new_collection_pid");
  }

  if ($form_state['clicked_button']['#id'] == 'mods_submit') {

    if (file_save_upload('upload') != 0) {
      $file = file_save_upload('upload');
      $file_url = str_replace(' ', '%20', $base_url . '/' . $file->filepath);
      drupal_set_message('File url: ' . $file_url);
      $contents = file_get_contents($file_url);
      $contents_header = substr(trim($contents), 0, 14);
      if ($contents_header == '<?xml version=' || $contents_header == '<mods xmlns="h') {
        $item = new Fedora_Item($collection_pid);
        $result = $item->add_datastream_from_string($contents, 'MODS', 'MODS record', 'text/xml', 'M', 'Added MODS record from MODS XML file');
        if ($result) {
          drupal_set_message(t('@file has been successfully uploaded', array('@file' => $file->filename)));
        }
        else {
          drupal_set_message(t('There was an error uploading the file'), 'error');
        }
      }
      else {
        drupal_set_message('Refworks contents: ' . $contents);
        $refworks_xml = new DOMDocument();
        $refworks_xml->loadXML($contents);
        $refworks = new DOMDocument();
        $xslt_path = drupal_get_path('module', 'islandora_govdocs') . '/xsl/refworks_to_mods.xsl';
        $refworks->load($xslt_path);
        $xslt = new XSLTProcessor();
        $xslt->importStylesheet($refworks);
        $new_mods = $xslt->transformToXml($refworks_xml);
        $item = new Fedora_Item($collection_pid);
        $result = $item->add_datastream_from_string($new_mods, 'MODS', 'MODS record', 'text/xml', 'M', 'Added MODS record from Refworks file');
        if ($result) {
          drupal_set_message(t('@file has been successfully uploaded', array('@file' => $file->filename)));
        }
        else {
          drupal_set_message(t('There was an error uploading the file'), 'error');
        }
      }
    }
    else {
      $mods_url = $form_state['values']['url'];
      $url_content = file_get_contents($mods_url);
      if ($url_content == FALSE) {
        drupal_set_message(t('@url is invalid', array('@url' => $mods_url)), 'error');
      }
      else {
        $item = new Fedora_Item($collection_pid);
        $result = $item->add_datastream_from_url($url_content, 'MODS', 'MODS record', 'text/xml', 'M', 'Added MODS record from external URL');
        if ($result) {
          drupal_set_message(t('MODS record has been updated successfully from @url', array('@url' => $mods_url)));
        }
        else {
          drupal_set_message(t('There was an error uploading the file'), 'error');
        }
      }
    }
  }

  if ($form_state['clicked_button']['#id'] == 'delete_collection') {
    $collection_name = $form_state['values']['collection_name'];
    $pids = get_related_items_as_array($collection_pid, 'isMemberOfCollection');

    $batch = array(
      'title' => "Deleting Objects from  $name",
      'operations' => array(),
      'file' => drupal_get_path('module', 'fedora_repository') . '/CollectionManagement.inc',
    );
    drupal_set_message('PIDs deleted: ' . implode(', ', $pids));
    foreach ($pids as $pid) {

      $batch['operations'][] = array('delete_objects_as_batch', array($pid));
    }

    batch_set($batch);
    batch_process('/fedora/repository/' . $collection_pid);
  }
}

function govdocs_add_pdf_form($form_state, $pid) {
  $form['#attributes'] = array('enctype' => "multipart/form-data");

  $form['pdf']['upload'] = array(
    '#type' => 'file',
    '#title' => t('Upload PDF file'),
    '#size' => 50,
  );

  $form['pdf']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Submit'),
    '#id' => 'pdf_submit',
  );

  $form['pdf_pid'] = array(
    '#type' => 'hidden',
    '#value' => $pid,
  );

  return $form;
}

function govdocs_add_pdf_form_submit($form, &$form_state) {
  global $base_url;
  $file = file_save_upload('upload');
  $pdf_url = str_replace(' ', '%20', $base_url . '/' . $file->filepath);
  $pid = $form_state['values']['pdf_pid'];
  $item = new Fedora_Item($pid);
  $result = $item->add_datastream_from_url($pdf_url, 'PDF', 'PDF');
  if ($result) {
    drupal_set_message(t('PDF successfully added to @pid', array('@pid' => $pid)));
  }
  else {
    drupal_set_message(t('An error occurred while adding the PDF!'), 'error');
  }
}
